# Week 10 — Middleware architecture: one boundary, written once

## 1. Concept

Weeks 8 and 9 produced a wire format, a service, and a way to find it — as two
programs that each did everything by hand.

```text
                        week 9       week 10
svc_client/main.cpp     711 lines    216
svc_server/main.cpp     492 lines    139
libs/av_mw              —            1952   shared, 10 integration tests
```

Be honest about that table: **total code went up.** The point is not fewer
lines. It is that the hard part now exists *once*, is tested, and every future
process — cluster, IVI, HUD, logger — gets it for free. Week 9's loopback bug,
copied into five programs, would have been five bugs.

A middleware is a boundary between what the application *means* and how the
bytes *move*. The week is about deciding what crosses it.

```text
application        vehicle->get_speed(callback)          floats and callbacks
════════════════════ the boundary ═════════════════════════════════════════
generated layer    vehicle.hpp: method ids, float <-> 4 bytes
middleware         Runtime · Proxy · Skeleton             SD, leases, sessions, timeouts
wire format        av_service: SOME/IP + SD bytes
transport          av_eth: UDP, multicast
```

## 2. Why it exists in a Cockpit DC

- **Many clients, same services.** Cluster, IVI, HUD, logger and diagnostics
  all want the vehicle speed. Seven hundred lines each is seven hundred lines
  of the same bugs, each fixed separately or not at all.
- **Many teams.** The HMI team should not need to know what a lease is. The
  boundary is an organisational one too — AUTOSAR Adaptive's `ara::com` is
  exactly this shape: a Proxy and a Skeleton generated from ARXML.
- **Failure handling has a natural split.** CLAUDE.md §7 asks for
  `Fault → Detection → Logging → Fallback → Safe State`. *Detection* and
  *logging* belong below the boundary (the `mw` log lines). *Fallback* belongs
  above it: only the application knows whether "speed unavailable" means grey
  out the needle or show a warning.

## 3. Architecture — the decisions

| Question | Decision | Why | What it costs |
|---|---|---|---|
| Who owns the threads? | **Nobody.** `poll()` runs on the caller's thread; `fd()` for someone else's loop | Qt (weeks 11–12) allows one UI thread; `QSocketNotifier` watches `fd()` | a slow callback stalls this process's networking — the same rule Qt already has |
| How does a call end? | **Exactly once**, in one of seven ways, **never inside `call()`** | caller code stays correct whatever order it is written in | a queue of posted completions in the runtime |
| Retry methods? | **Never.** FindService: always | `UnlockDoors()` whose *reply* was lost has already run | the application decides |
| Too many calls? | **Refuse** with `Busy` past 16 | a queue to a dead service only delays the bad news | callers must handle `Busy` |
| Service withdrawn? | waiting calls fail **at once** with `NotAvailable` | `Timeout` would arrive later *and* name the wrong cause | — |
| Ownership | Runtime outlives Proxy/Skeleton; Skeleton destructor = StopOffer | a destructor cannot forget, and runs on every early return | declaration order matters |

What one `poll()` does, in order:

```text
poll(timeout)
  1. epoll_wait       dispatch reads: SD socket, method sockets, event sockets
  2. expire leases    a lease ran out -> proxies fail waiting calls, leave group
  3. repeat finds     FindService for anything not yet found
  4. repeat offers    OfferService every interval
  5. ticks            proxies check call timeouts
  6. posted work      completions decided earlier (NotAvailable, Busy)
```

Leases before calls, so a service that just vanished is not called again in
the same round.

**Honest comparison.** vsomeip made both opposite choices: it owns threads, and
it runs one routing manager per host that every process talks to locally. Both
are defensible. Owning threads is right for a process with no event loop of
its own; the routing manager solves a problem this week *measured* (§4).

## 4. C++ / Linux implementation

### The boundary is enforced by the build

The Runtime is a pimpl, and the Proxy and Skeleton state lives in the `.cpp`
files. No `av_mw` header names a socket, so:

```cmake
target_link_libraries(av_mw PUBLIC av_service PRIVATE av_eth av_ipc ...)
target_link_libraries(svc_client PRIVATE av_mw av_log)   # was: av_service av_eth av_ipc
```

`#include "av/eth/udp_socket.hpp"` in an app no longer compiles. The compiler
reviews the boundary, and it does not get tired.

### Never call back from inside `call()`

```cpp
proxy.call(..., [&] { waiting = false; });
waiting = true;
```

If a failure known at once (not discovered, too busy) ran the callback
immediately, `waiting` would be cleared *before* it was set — and stay `true`
forever. So every completion, even an instant one, is posted to the next
`poll()`. Test: `ACallBeforeDiscoveryFailsLocallyAndNeverInsideCall`.

### Exactly once: out of the table before the handler runs

```cpp
Pending call = std::move(found->second);
pending.erase(found);      // first
call.handler(result);      // may call again, or destroy the proxy
```

A late reply to a call already timed out finds nothing in the table and is
dropped — that is what the session id is for.

### A callback may destroy its own proxy

The Proxy holds a `shared_ptr<State>`; every handler the runtime keeps holds a
`weak_ptr`. At dispatch the weak pointer is locked, so the state stays alive
until the callback returns, even if the callback destroyed the Proxy. And the
dispatcher copies each `std::function` before calling it, because the owner may
unregister — destroying that very function — from inside the call. Week 1's
iterator invalidation, met again in an event loop. Test:
`AProxyMayBeDestroyedFromInsideItsOwnCallback`, under ASan.

### Why no automatic retry

The middleware sees bytes and a method id. It cannot know that `GetSpeed` can be
repeated and `UnlockDoors` cannot. A timeout after a *lost reply* means the
command already ran; retrying runs it twice. **Idempotency decides retry, and
only the application knows it.** FindService is the exception because asking
again is always harmless.

### Found by measuring: unicast to a shared port

```text
two sockets, SO_REUSEADDR, same port, same host
  A (bound first)    received ['multi']
  B (bound second)   received ['uni0', 'uni1', 'uni2', 'multi']
```

Multicast reaches both; **unicast reaches only the socket bound last.** Week
9's server answered FindService by unicast to `127.0.0.1:30490` — and whenever
the server started second, that answer went back to the server itself. The
client only ever learned from the cyclic multicast offer, which arrived so
quickly that no log showed the difference. av_mw answers FindService by
multicast. On a vehicle every ECU has its own address and unicast is fine; on
one host, this is precisely why vsomeip puts one routing manager in front of
every process.

### Deliberate behaviour changes

- A **version mismatch** is refused by the *server* with
  `E_WRONG_INTERFACE_VERSION`. Week 8's server answered anyway and left the
  client to refuse. The side that knows its own version now says so.
- **Session ids no longer appear** in the apps' output. They belong to the
  middleware now, which is the point.

## 5. Hands-on exercise

The same commands as week 9 — every flag still works.

```bash
./build/debug/apps/svc_client/svc_client      # terminal 1, FIRST
./build/debug/apps/svc_server/svc_server      # terminal 2, a few seconds later
```

Two voices in the client's output, and learning to tell them apart is the
exercise:

```text
[cli] <-- NOT_AVAILABLE   (not discovered yet, or withdrawn -- nothing was sent)
INFO  mw  service available service=4660 instance=1
INFO  mw  joined the event group group=239.10.0.2 port=30510
[cli] +++ AVAILABLE    -- calls will now be sent
      ... responses and events ...
INFO  mw  StopOffer -- the service was withdrawn on purpose            <- Ctrl-C
INFO  mw  left the event group group=239.10.0.2 port=30510
[cli] !!! UNAVAILABLE  -- calls fail locally until it returns
WARN  mw  peer rebooted -- its session ids went backwards was=5 now=1  <- restart
[cli] +++ AVAILABLE    -- calls will now be sent
WARN  mw  call timed out -- lost request, lost reply, or a wedged server  <- kill -9
[cli] <-- TIMEOUT   (sent, no answer: lost datagram or a wedged server)
WARN  mw  lease expired -- nobody said goodbye
[cli] !!! UNAVAILABLE  -- calls fail locally until it returns
```

`mw` lines say **why** — for whoever debugs the network. `[cli]` lines say
**what it means** — for whoever writes the screen. That split *is* the boundary.

```bash
svc_client --method 0x0009                     # <-- ERROR E_UNKNOWN_METHOD
svc_server --wrong-version  +  svc_client      # <-- ERROR E_WRONG_INTERFACE_VERSION
svc_server --no-sd          +  svc_client --static   # works: both believe the constant
svc_server --no-sd          +  svc_client      # NOT_AVAILABLE forever, nothing sent
```

## 6. Failure / debugging exercise

| Fault | Inject it by | The application sees | The `mw` log says |
|---|---|---|---|
| Not offered | `svc_server --no-sd` | `NOT_AVAILABLE`, nothing sent | FindService (at debug level) |
| Clean shutdown | Ctrl-C the server | `UNAVAILABLE` at once, 0 timeouts | `StopOffer -- withdrawn on purpose` |
| Crash | `kill -9` the server | 2 × `TIMEOUT`, then `UNAVAILABLE` | `call timed out` ×2, `lease expired` |
| Restart | stop, start | `AVAILABLE` | `peer rebooted ... was=5 now=1` |
| Missing method | `--method 0x0009` | `ERROR E_UNKNOWN_METHOD` | server: `no handler for this method` |
| Wrong version | `--wrong-version` | `ERROR E_WRONG_INTERFACE_VERSION` | server: `caller speaks another interface version` |
| Too many calls | test `BeyondMaxPending...` | `BUSY`, nothing sent | — |

Break a rule and watch a test catch it:

1. In `Proxy::State::post_failure`, call `handler(...)` directly instead of
   posting it. `ACallBeforeDiscoveryFailsLocallyAndNeverInsideCall` fails on
   `EXPECT_FALSE(completed)`.
2. In `Proxy::State::take`, invoke the handler *before* erasing it from
   `pending`. `AProxyMayBeDestroyedFromInsideItsOwnCallback` then erases
   through an iterator the callback already invalidated — run the `asan`
   preset and read the report.

## 7. Senior interview questions

1. What belongs on each side of a middleware boundary? Where do detection and
   fallback live?
2. Should a middleware own threads? Argue both sides, and say what vsomeip
   chose.
3. How does a runtime with no thread plug into Qt's event loop? What must
   still run when nothing arrives?
4. Why must a completion never run inside the call that started it?
5. How do you guarantee a completion handler runs exactly once?
6. Should a middleware retry failed method calls? What property decides?
7. Under backpressure, queue or refuse? Defend the choice.
8. A service disappears with five calls pending. What should each caller see,
   and when?
9. How can a callback safely destroy the object that invoked it?
10. How do you make an API boundary enforced by the build instead of by review?
11. Why does a Skeleton not offer when constructed, but withdraw when
    destroyed?
12. Why does the generated layer stay thin, and what does that make possible?

## 8. Review checklist

- [ ] I can draw the five layers and say what crosses the boundary.
- [ ] I can defend "no thread" and state its one cost.
- [ ] I can list the seven ways a call ends, and which two send nothing.
- [ ] I can explain the `waiting = true` bug that rule 2 prevents.
- [ ] I can explain why the middleware never retries a method.
- [ ] I can explain `take()`-before-handler and `weak_ptr` in handlers.
- [ ] I can explain why the apps cannot include a socket header any more.
- [ ] I can explain the SO_REUSEADDR unicast finding and what it implies.
- [ ] I have run the lifecycle demo and can match every `mw` line to its cause.

---

**Still open at the end of week 10.**

- **The runtime is single-threaded by contract, not by lock.** Calling it from
  two threads is a data race, and TSan would say so. That is a deliberate
  consequence of "no thread", and the thing week 12 must respect when Qt
  enters.
- **SubscribeEventgroup / Ack** — still parsed, still never sent; carried from
  week 9.
- **`vsomeip`**, still deferred. This week's design is now concrete enough to
  compare against it line by line — routing manager, thread pool, and all.
- **The `fd()` integration is untested** until a real foreign loop drives it.
  That is week 12.

**Next:** week 11, Qt core — QObject, signals and slots, the event loop, and a
demo that blocks the loop and freezes the UI. The rule this week put on
callbacks — never block — is about to meet the framework that made it famous.
