# Week 11 — Qt core: one thread, one loop, one queue

## 1. Concept

Week 10 built a middleware that owns no thread. `Runtime::poll()` runs callbacks
on the caller's thread, so one slow callback stalls the whole process, and
`av_mw` therefore put a rule on every handler: **return**.

Qt is that same contract with a framework's vocabulary. Learning it is not
learning a new model — it is finding out that the thing already built by hand
has standard names.

```text
av_mw                                Qt
while (!stop) runtime->poll()        QApplication::exec()
callback must not block              slot must not block
runtime->post(fn)                    Qt::QueuedConnection
weak_ptr in every stored handler     QObject destructor disconnects
runtime->fd()                        QSocketNotifier              <- week 12
```

Which is why this week's artifact is not a UI. It is **a ruler and a broken
thing to measure**: `av::qt::LoopMonitor`, and three buttons that each do
exactly three seconds of work.

## 2. Why it exists in a Cockpit DC

- **A cluster is a soft-real-time deadline with a needle attached.** 60 Hz is a
  16 ms budget. Anything that occupies the UI thread for longer is not slow, it
  is *wrong*: the speed on screen is stale, and the driver cannot tell.
- **Everything a cockpit wants to do is blocking by nature.** A `read()` on a
  CAN socket, a file on flash, a SOME/IP call to another ECU. The loop cannot
  tell a sleep from an honest blocking syscall — all it knows is that it did not
  get control back.
- **"The UI hung" is a measurement, not an opinion**, and week 15's fault
  injection needs it to be. `worst 3012 ms` can go in a bug report; "feels
  laggy" cannot.

## 3. Architecture — the decisions

| Question | Decision | Why | What it costs |
|---|---|---|---|
| Where does the instrument live? | `libs/av_qt`, depending on **nothing else in this tree** | week 11 is Qt's own model; week 12 is where av_can and av_mw meet it. An av_qt that already knew about vehicle signals could not show where the boundary is | the demo and the library must be read together |
| What is tested vs what is watched? | av_qt tests the *semantics*; `apps/qt_loop` holds the three *cures* | when a slot runs, who disconnects it, which thread it runs on — those are assertable. A frozen window is not | the cures are demo code, proven by the tests' properties rather than directly |
| Widgets or QML? | **Widgets** | the event loop is identical in both, and Widgets needs no second language. Week 12 then starts QML clean | week 11's UI code is thrown away |
| Qt in the build? | **optional** (`AV_HAVE_QT`) | a machine without Qt must still build weeks 1–10 | one `if()` in two CMakeLists |
| Qt under TSan? | **skipped, loudly** | the installed Qt is not instrumented, so every handoff inside QThread reports as a race *in our stack trace*. Suppressing them one by one would hide the real ones | the thread story is proved under ASan and by an explicit `wait()` instead |

## 4. C++ / Linux implementation

### The measurement, and the point of the week

All four rows below do the same amount of work. Only the shape differs. Taken
from `apps/qt_loop`, driven through `QAbstractButton::click()`:

```text
                          ticks    p50    p99    worst
0. nothing pressed          305   15 ms  16 ms    17 ms
1. Block 3 s                125   15 ms  17 ms  3012 ms   <- 177x the budget
2. Chunk it (10 ms)         311   15 ms  22 ms    34 ms
3. Worker thread            312   15 ms  16 ms    17 ms
```

Three things in that table are worth more than the headline.

**`p50` is 15 ms in every row.** A median cannot see a three-second freeze. Nor
can a mean: 3000 ms spread over 300 samples is 10 ms of average. The only column
that noticed is `worst`, which is why the readout reports it — and why a cockpit
latency budget is written in percentiles, never in averages.

**`ticks` fell from 305 to 125.** A QTimer that could not fire does **not** fire
N times afterwards to catch up; Qt drops what it missed. So a blocked loop does
not merely arrive late, it *loses* work — and anything counting ticks to measure
time silently under-counts.

**Chunking cost something: 34 ms, not 17.** Handing control back every 10 ms
still means the 16 ms timer can be a slice late. It is 88× better than blocking
and it is not free, and saying so is more useful than a round number.

### The three cures, and when each is right

```cpp
// 1. the bug
void Window::block_the_loop() { occupy(3000ms); }         // worst 3012 ms

// 2. slice it -- no thread, no mutex, no new failure mode
chunk_timer_.setInterval(0);                              // "next time round"
void Window::run_one_chunk() { occupy(10ms); if (--left_) return; finish(); }

// 3. move it -- the work is genuinely blocking and cannot be sliced
worker_.moveToThread(&worker_thread_);
emit work_requested(3000);                                // queued, by itself
```

Cure 2 is the right answer whenever the work *can* be divided: no second thread
means no race, no cancellation problem, nothing to join. Cure 3 is for work that
cannot — a blocking `read()`, a library that does not return — and it buys that
with three new obligations: cancellation, shutdown order, and never touching a
widget from over there.

### Ownership: Qt's answer to week 10's `weak_ptr`

```cpp
QObject::connect(&sender, &Sender::pinged, doomed.get(), &Receiver::on_pinged);
doomed.reset();
sender.fire(3);        // the dead receiver is dropped; the live one still fires
```

Nobody called `disconnect()`. A `QObject` unregisters itself from every
connection in its destructor — exactly what week 10 spent a `shared_ptr` /
`weak_ptr` pair to build by hand.

The trap is the lambda, because a lambda has no destructor Qt can hook:

```cpp
connect(&sender, &Sender::pinged, context, [&ran] { ran = true; });  // safe
connect(&sender, &Sender::pinged,          [&ran] { ran = true; });  // compiles
```

The second overload exists, never disconnects, and reads freed memory once
whatever it captured dies. **The context object is the lifetime**, and omitting
it is not a shorthand.

### Thread affinity is one-way

```cpp
receiver->moveToThread(&worker);   // allowed: pushed by the thread it lives on
receiver->moveToThread(here);      // refused: "Cannot move to target thread"
```

An object may be *pushed* to another thread by the thread it currently lives on,
and cannot be *pulled* back from outside. So the pattern is: construct, move
once, leave alone — and connect `QThread::finished` to `deleteLater` rather than
deleting across the boundary. This was learned by writing the symmetric call and
reading Qt's refusal on stderr.

### No mutex anywhere

```cpp
connect(&worker_, &Worker::progressed, progress_, &QProgressBar::setValue);
worker_.moveToThread(&worker_thread_);          // connected first, moved after
```

The connection survives the move, and `Qt::AutoConnection` decides direct or
queued **at each emit**, from the affinities it finds *then*. Sender on the
worker thread, receiver on the UI thread, so Qt posts an event. That posted
event is the synchronisation — week 2's `BoundedQueue` and week 10's `post()`,
supplied by the framework.

The one thing that is *not* a signal is cancellation:

```cpp
void cancel() { cancelled_.store(true); }   // callable from any thread
```

A slot would be queued *behind* the very work it is meant to interrupt, and
would run only after it finished. Hence an atomic, and hence week 2 again.

### Two link errors and a real bug, none of them guessed

**`undefined reference to LoopMonitor::staticMetaObject`.** AUTOMOC only mocs a
header that is named in the target's `SOURCES`, or that sits next to a
same-named source. Ours is in `include/av/qt/` while the source is in `src/`, so
moc never saw the `Q_OBJECT`. The fix is to list the header as a source although
nothing compiles it:

```cmake
add_library(av_qt src/loop_monitor.cpp include/av/qt/loop_monitor.hpp)
set_target_properties(av_qt PROPERTIES AUTOMOC ON)
```

**A half-pixel wobble.** `painter.translate(width() / 2, height() / 2)` —
`translate` takes `qreal`, so the integer division throws the half away *before*
the promotion, and on any odd width the needle pivots off centre. Found by
`bugprone-integer-division`, not by looking at it. Exactly the bug class week 1's
`-Wconversion` argument was about.

### clang-tidy meets a framework older than the guidelines

Five checks were switched off in the Qt directories, and the reasons are not
interchangeable.

| Check | Why the advice cannot be taken |
|---|---|
| `readability-identifier-naming` | `paintEvent`, `sizeHint` are camelBack because the base class is; an override that renames is not an override |
| `cppcoreguidelines-owning-memory` | `new QPushButton(this)` is not a leak. QObject parenting *is* RAII — the parent's destructor is the release. A `unique_ptr` would make two owners of one object |
| `readability-redundant-access-specifiers` | `signals:` and `public slots:` both expand to `public:`. Deleting `signals:` does not tidy the class, it stops moc from compiling it |
| `misc-include-cleaner` | it names `qnamespace.h` and `qobjectdefs.h`, headers no Qt program includes. Obeying it means `#include <QtCore/qobjectdefs.h>` in order to get a macro |

The fifth is different, and worth remembering:

> `misc-const-correctness` asked for `const Receiver receiver` and
> `const QApplication app`, because the surrounding code never writes to them.
> **Qt writes to both** — through a connection, and through the `qApp` pointer
> its constructor installed. Declaring an object `const` and then modifying it is
> undefined behaviour. Here the linter does not suggest a cleanup; it suggests a
> bug.

Every finding that *could* be fixed was fixed first: four missing includes in
`loop_monitor.cpp`, five in the app, and the integer division above. The checks
stay on everywhere else in the tree.

## 5. Hands-on exercise

```bash
./build/debug/apps/qt_loop/qt_loop
```

Press **1**, **2**, **3** in turn, reading `worst` after each, resetting between.
Then do it properly:

1. Press **1**, and while the window is frozen, click **Click me** five times.
   The counter does not move — and then jumps by five. The clicks were never
   lost; they were queued. *That is what "the UI hung" means.*
2. Watch the needle rather than the numbers. It is driven by a 16 ms QTimer and
   nothing else; it stops because the loop stopped, and no code had to notice.
3. Press **2** and **3** and try to tell them apart *by looking*. You cannot —
   which is the case for cure 2 whenever the work can be divided.

## 6. Failure / debugging exercise

| Fault | Inject it by | What you see | The number |
|---|---|---|---|
| Blocked slot | button 1 | needle stops, clicks queue | `worst 3012 ms` |
| Blocked slot, hidden | read `p50` after button 1 | nothing at all | `p50 15 ms` |
| Lost timer ticks | compare `ticks` after 1 vs 0 | — | `125` vs `305` |
| Chunking is not free | button 2, read `p99` | — | `22 ms`, up from `16` |
| Missing moc | drop the header from `SOURCES` | link error | `undefined reference to staticMetaObject` |
| Dangling lambda | drop the context object from a `connect` | works, then reads freed memory | ASan, under the `asan` preset |
| Wrong-thread move | add `moveToThread(back)` | `Cannot move to target thread` on stderr | — |

Break a rule and watch it bite:

1. In `Worker::run`, delete the `cancelled_` check. `Window`'s destructor now
   sits in `wait()` for up to three seconds — with button 3 pressed, closing the
   app becomes visibly slow.
2. Change `Qt::QueuedConnection` to `Qt::DirectConnection` in
   `AQueuedConnectionWaitsForTheNextTripThroughTheLoop`. It fails on
   `EXPECT_EQ(receiver.calls, 0)` — the slot ran inside `fire()`, which is week
   10's `waiting = true` bug in a new costume.

## 7. Senior interview questions

1. Why must GUI work not block the event loop? Answer in milliseconds.
2. A QTimer is set to 16 ms and a slot blocks for 3 s. How many times does the
   timer fire during those 3 s, and how many times afterwards?
3. Why is `p50` the wrong statistic for UI latency, and `worst` the right one?
4. When does `Qt::AutoConnection` deliver directly and when does it post? *When*
   is that decided?
5. Three cures for a long operation on the UI thread. Which needs no mutex, and
   why is that the default answer?
6. How does Qt stop a signal reaching a destroyed receiver? What does a lambda
   break about that, and what restores it?
7. Can you move a QObject back to the thread that created it? Why not?
8. Why is `Worker::cancel()` an atomic rather than a slot?
9. Where does `staticMetaObject` come from, and name two ways to fail to get it.
10. Qt is not instrumented for TSan. How do you keep a threading claim honest
    without it?
11. A linter tells you to make an object `const`, and the object is a
    `QApplication`. What do you do, and why?
12. `av_mw::Runtime` owns no thread and exposes an `fd()`. How does that reach
    Qt's event loop?

## 8. Review checklist

- [ ] I can draw the av_mw ↔ Qt mapping table from memory.
- [ ] I can explain why `p50` was 15 ms in all four rows.
- [ ] I can explain why `ticks` fell to 125 and were never made up.
- [ ] I can name the three cures and say which to reach for first.
- [ ] I can explain what a context object does in a `connect` to a lambda.
- [ ] I can explain why affinity is one-way.
- [ ] I can explain why no mutex appears anywhere in `qt_loop`, and why
      `cancel()` is the exception.
- [ ] I can explain the `staticMetaObject` link error, both causes.
- [ ] I can defend all five clang-tidy suppressions, and say which one was
      pointing at undefined behaviour.
- [ ] I have run the demo, queued five clicks into a frozen window, and read
      `worst` after each button.

---

**Still open at the end of week 11.**

- **`fd()` is still untested.** `av_mw::Runtime::fd()` exists so a foreign loop
  can drive the middleware, and no foreign loop has yet. That is week 12's first
  job, with `QSocketNotifier`.
- **Nothing in `av_qt` knows what a vehicle is,** deliberately. The seam between
  Qt and `av_can`/`av_mw` is still visible because nothing has crossed it.
- **The needle is not a cluster.** It has no data behind it — it is a liveness
  indicator wearing a dial. Week 13 gives it something to point at.
- **Widgets, not QML.** Everything learned here about the loop, affinity and
  queued connections carries over unchanged; the widget code does not.

**Next:** week 12, C++ ↔ QML — `Q_PROPERTY`, a `VehicleModel`, and
`QSocketNotifier` wiring a CAN fd into the event loop. The rule this week
measured is about to meet real data arriving on a file descriptor.
