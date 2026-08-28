# Week 2 — The cockpit thread model

## 1. Concept

Three threads, two queues, and a rule for each boundary:

```text
  [rx]  --frames-->  [decode]  --state-->  [ui]
   |                    |                     |
never blocks       blocks upstream       may be slow
                   never downstream
```

A bounded queue is not primarily a container. It is a **contract about what
happens when the consumer cannot keep up**, and there are only three answers:

| Policy | When it is right | Cost |
|---|---|---|
| block the producer | producer can afford to wait; losing data is worse than delay | back-pressure propagates upstream, possibly to a place that cannot wait |
| drop newest | event stream where order matters and old events still count | the newest information is the one you lose |
| drop oldest | latest-value state (speed, RPM, temperature) | history is lost; fine, nobody wanted it |

`BoundedQueue` deliberately implements **none** of them. It offers `push`
(blocks) and `try_push` (reports `Full`), and each caller picks. A queue that
silently drops is a queue that lies to its producer.

The second idea is **close-then-drain**:

```cpp
while (const auto item = queue.pop()) { /* ... */ }   // ends by itself
```

`close()` stops new pushes and wakes every waiter, but `pop()` keeps returning
queued items until there are none left. Only then does it return `nullopt`.
That single rule removes the stop-flag, the polling timeout, and the "did we
lose the last three frames on shutdown" question.

## 2. Why it exists in a Cockpit DC

The cluster draws at 60 Hz. CAN delivers at up to 1 kHz. Those two rates will
never match, and the mismatch has to be absorbed *somewhere explicit*.

- If the receive thread blocks, the kernel's CAN socket buffer fills behind it
  and the driver drops frames — the same loss, but now invisible and
  unattributable.
- If the queue is unbounded, a UI stall becomes a memory leak, then an OOM kill
  of the cluster process. On a real ECU there is no swap to hide it.
- If shutdown is not graceful, an ignition-off leaves half-processed frames and
  a process that has to be SIGKILLed. That is also how you lose diagnostic
  trouble codes that were queued but never written.

The demo makes the trade-off measurable rather than theoretical: 200 frames in,
25 displayed, 175 explicitly overwritten, **0 lost silently**.

## 3. Architecture

```text
                        [ Cluster UI ]              week 13
                              |
                        [ Qt model ]                week 12
                              |
                        [ Middleware ]              weeks 8-10
                              |
    +-------------------------+-------------------------+
    |            BoundedQueue<VehicleState>             |  <- HERE
    |            capacity 4, drop-oldest                |
    +-------------------------+-------------------------+
                              |
                    [ decode thread ]                     <- HERE
                              |
    +-------------------------+-------------------------+
    |            BoundedQueue<CanFrame>                 |  <- HERE
    |            capacity 32, drop-on-full              |
    +-------------------------+-------------------------+
                              |
                      [ rx thread ]                       <- HERE
                              |
                  [ SocketCAN / vcan0 ]               week 5
```

`av_conc` and `av_ipc` (week 3) stay separate libraries on purpose: one crosses
a **thread** boundary, the other an **address space**. The failure modes are
different and the code should not pretend otherwise.

## 4. C++ / Linux implementation

| File | Role |
|---|---|
| [libs/av_conc/include/av/conc/bounded_queue.hpp](../libs/av_conc/include/av/conc/bounded_queue.hpp) | the whole library — header-only template |
| [libs/av_conc/tests/bounded_queue_test.cpp](../libs/av_conc/tests/bounded_queue_test.cpp) | 16 tests, incl. 3 concurrency tests that exist for TSan |
| [apps/cockpit/main.cpp](../apps/cockpit/main.cpp) | three threads, two queues, ordered shutdown, accounting |

### Design decisions worth defending

**Ring buffer over `std::queue`.** `std::queue` allocates on every push. Week 1
chose `std::array` over `std::vector` inside `CanFrame` for exactly this reason;
using a `std::deque` here would have thrown that away one layer up. The cost is
a `static_assert` that `T` is default-constructible, because the slots are
constructed once at start-up.

**Two condition variables, not one.** A consumer finishing a `pop` should wake a
waiting *producer*. With a single CV you wake everyone and most go straight back
to sleep — a thundering herd that shows up as CPU time under load.

**`notify_one` normally, `notify_all` on close.** Close changes a fact every
waiter must observe; a single notification would leave the rest asleep forever.

**Notify outside the lock.** Waking a thread that then immediately blocks on the
mutex you still hold is wasted context switching.

**Predicate form of `wait`.** `wait(lock, pred)` re-checks after every wakeup.
The bare `wait(lock)` is wrong here — spurious wakeups are real, and so is the
lost-wakeup race if the notify lands before the wait.

**Constructor throws; the data path does not.** `capacity == 0` is a programming
error caught once at start-up, where an exception is the loudest and cheapest
signal. Everything on the data path returns a status instead. `main()` is a
`try`/`catch` around `run()` so nothing escapes — an exception leaving `main` is
`std::terminate` with no message.

**`size()` is documented as a lie.** It is true at the instant the lock is
released and stale immediately after. Fine for logging; branching on it is a
race by construction.

## 5. Hands-on exercise

```bash
./check.sh              # debug + asan + ubsan + tsan + clang-tidy
./build/debug/apps/cockpit/cockpit
```

Expected shape of the output:

```text
  produced            200
  frames dropped      0     (rx queue full)
  decoded             200
  decode failures     0
  states overwritten  175   (ui queue full)
  displayed           25
```

The last three lines are the lesson. The UI runs at 10 ms per frame against a
1 ms producer, so roughly nine out of ten states are superseded before they can
be drawn — and the program *says so* instead of pretending.

Things to try:

1. Raise `states` capacity from 4 to 200. Overwrites drop to zero, latency to
   the UI grows to two seconds. Bigger buffers do not fix a rate mismatch; they
   convert dropped data into stale data.
2. Change the decoder's downstream `try_push` to a blocking `push`. The decoder
   inherits the UI's rate, the frame queue backs up, and rx starts dropping.
   Back-pressure has moved to the one thread that cannot absorb it.
3. Delete `frames.close()`. The decode thread blocks in `pop()` forever and
   `join()` never returns — the classic shutdown hang, in three lines.

## 6. Failure / debugging exercise

| Fault injected | Detected by | Logged as | Fallback | Safe state |
|---|---|---|---|---|
| UI slower than rx | `try_push` returns `Full` | counted in `states_overwritten` | drop oldest, keep newest | latest value always shown; staleness handled in week 8 |
| rx queue full | `try_push` returns `Full` | counted in `frames_dropped` | drop the frame | — |
| Truncated frame | `decode` returns `nullopt` | `WARN decode incomplete frame` | frame skipped, no state produced | week 1's rule: absence != zero |
| `close()` before consumers drain | — | — | cannot happen: `pop` drains first | shutdown loses nothing |
| `close()` never called | **nothing** — `join()` hangs | nothing | none | this is why shutdown order is code, not convention |

The last row is week 2's counterpart to week 1's silent byte-order fault: a
deadlock produces **no** output at all. No exception, no log line, no crash
dump — just a process that never exits. The tests `Close.WakesABlockedPop` and
`Close.WakesABlockedPush` exist precisely because a regression there is
invisible until it hangs CI.

### Why TSan matters more than the tests passing

`Concurrency.EveryItemArrivesExactlyOnce` runs 4 producers and 3 consumers
through a 16-slot queue. It passes with a broken lock too, most of the time.
TSan is what turns "passed" into evidence: it instruments every memory access
and reports the race whether or not it changed the result on this run.

## 7. Senior interview questions

**1. Why bound the queue at all? Memory is cheap.**

Because an unbounded queue converts a rate mismatch into unbounded memory, and
the failure moves from "we drop frames" (visible, attributable, recoverable) to
"the cluster process is OOM-killed" (fatal, and 40 seconds after the actual
cause). Bounding also makes the trade-off a decision someone made, rather than
one the allocator makes for you.

**2. Blocking `push` or `try_push` — how do you choose?**

By asking what the producer can do with the answer. A CAN receive thread cannot
usefully wait: the kernel socket buffer behind it overflows and drops the same
frames, only silently. A file-replay thread can wait, and should. The rule is
that back-pressure must stop at the first component able to absorb it, and
never propagate into one that cannot.

**3. Why does `pop()` drain after `close()` instead of returning immediately?**

Because closing means "no more work will arrive", not "discard the work you
have". Returning immediately would lose whatever was queued at the moment of
shutdown — for a cockpit, potentially the frames carrying the fault that caused
the shutdown. Draining also gives consumer loops a natural exit with no stop
flag and no timeout.

**4. Why two condition variables?**

They express two different predicates: "not full" for producers, "not empty"
for consumers. One CV would have to wake every waiter on every state change,
and each would re-evaluate a predicate that is false for most of them. The
thundering herd is measurable under contention.

**5. `size()` returns 4 and capacity is 4. Can you conclude the queue is full?**

No. The value was true while the lock was held and may already be wrong. Any
`if (queue.size() < queue.capacity()) queue.try_push(...)` is a check-then-act
race. That is why `try_push` does the check *inside* the same critical section
as the write, and why `size()` is documented as being for logging only.

**6. Where is priority inversion in this design?**

Not present yet, because every thread runs at the default priority. It becomes
real in week 12: if the UI thread runs at higher priority than the decoder and
they contend on the same mutex, the UI can be blocked by a low-priority thread
holding it, while a medium-priority thread preempts the decoder. The fix is
priority inheritance on the mutex (`PTHREAD_PRIO_INHERIT`), which is why QNX
makes it the default and Linux does not.

## 8. Review checklist

- [x] I can explain this without notes
- [x] I implemented it, not just read it
- [x] I broke it on purpose and watched it fail — overwrite under backpressure,
      truncated frame, and the shutdown hang from a missing `close()`
- [x] I can defend the design trade-off I chose — bounded over unbounded,
      caller-chosen policy over built-in dropping, close-then-drain over a flag
- [x] Tests pass under `./check.sh` — debug / asan / ubsan / **tsan** / tidy clean

### Left for later, deliberately

- **Lock-free SPSC queue** -> after week 15. A mutex-based queue is fast enough
  to reach the capstone, and "we replaced it because a profile said so" is a
  better story than "we wrote it lock-free first".
- **Timeouts** (`push_for` / `pop_for`) -> week 8, where the middleware needs a
  deadline rather than an indefinite wait.
- **Staleness / last-known-good** -> week 8. Right now a dropped state is simply
  gone; the vehicle data model is what turns that into "value is 3 seconds old".
- **Thread priorities and CPU affinity** -> week 3, with the rest of the Linux
  scheduling material.
