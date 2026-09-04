# Week 5 — SocketCAN: the bus becomes a file descriptor

## 1. Concept

The striking thing about SocketCAN is how little of it is new. A CAN bus is
reached with `socket()`, `bind()`, `read()`, `write()` — the same calls week 3
used for AF_UNIX, with a different address family:

```c
socket(PF_CAN, SOCK_RAW, CAN_RAW)
```

Everything week 3 built therefore still works. `Poller` watches a CAN socket
with no change at all, because a CAN socket *is* an ordinary descriptor. That
is the design win: Linux exposes a fieldbus through an API every programmer
already knows, instead of a bespoke driver ioctl set.

Three things genuinely differ, and each one has bitten somebody:

| | What it is | What goes wrong |
|---|---|---|
| **ifindex, not a name** | `bind()` takes a kernel interface *index*; the name must be translated first | "vcan0" is not something you can bind to directly; and vcan is not persistent, so it is simply gone after a reboot |
| **Flags live inside `can_id`** | the top 3 bits are `CAN_EFF_FLAG`, `CAN_RTR_FLAG`, `CAN_ERR_FLAG` | an unmasked read reports `0x81ABCDEF` instead of `0x1ABCDEF`, and every DBC lookup misses |
| **Size is the discriminator** | `read()` returns 16 bytes (classic) or 72 (FD) | there is no flag to consult; a receiver that assumes one size mis-parses the other |

Two more facts that only matter once, but matter a lot:

- **Filtering happens in the kernel.** `CAN_RAW_FILTER` decides which frames
  are ever copied to the process. On a busy bus this is the difference between
  waking for every frame and waking for the four that matter.
- **An empty filter list means "deliver nothing"**, not "deliver everything".
  A receiver that clears its filters to "reset" them goes permanently silent.

## 2. Why it exists in a Cockpit DC

This is the week the abstraction stops. Weeks 1–4 modelled CAN; week 5 talks to
it. Three consequences for a cockpit:

- **The cluster's receive path is now a real fd**, so it composes with the
  event loop everything else uses — the same `epoll` here, the same
  `QSocketNotifier` in week 12. There is no separate "CAN thread" unless a
  measurement asks for one.
- **`vcan` makes the whole chain testable without hardware.** The capstone can
  be developed, tested and demonstrated on a laptop, and the only thing that
  changes on the target is the interface name.
- **Week 4's counters become observable.** With `CAN_RAW_ERR_FILTER` on, the
  kernel delivers TEC and REC as ordinary reads. The silent fault that closed
  week 4 — a node goes bus-off and the bus looks *healthier* — is no longer
  silent to a program that asks.

## 3. Architecture

```text
     sim_vehicle                              can_rx
   (encode 7 signals)                    (decode, detect staleness)
          |                                     ^
       write()                                read()
          |                                     |
          v                                     |
   +---------------------------------------------------+
   |                  vcan0 (kernel)                    |
   |     acceptance filters . loopback . error frames   |
   +---------------------------------------------------+
                          |
                  (on real hardware)
                    CAN controller          <- week 4: arbitration, TEC/REC
                          |
                     CAN_H / CAN_L
```

`can_rx` puts week 3 and week 1 back to back for the first time:

```text
Poller (wk 3) -> CanSocket::receive (wk 5) -> decode (wk 1) -> physical value
```

## 4. C++ / Linux implementation

| File | Role |
|---|---|
| [socket.hpp](../libs/av_can/include/av/can/socket.hpp) | `CanSocket`, `CanFilter`, `SocketStatus`, `FrameKind`, `BusErrorInfo` |
| [socket.cpp](../libs/av_can/src/socket.cpp) | `PF_CAN` socket, `if_nametoindex` + `bind`, filters, FD mode, error frames |
| [socket_test.cpp](../libs/av_can/tests/socket_test.cpp) | 13 cases; the 9 that need `vcan0` skip themselves when it is absent |
| [common/vehicle_signals.hpp](../apps/common/vehicle_signals.hpp) | the seven signals of CLAUDE.md §7, in two messages, shared by both apps |
| [sim_vehicle/main.cpp](../apps/sim_vehicle/main.cpp) | 10 Hz transmitter |
| [can_rx/main.cpp](../apps/can_rx/main.cpp) | epoll receiver, kernel filters, staleness detection, bus-error reporting |

Decisions worth defending:

- **`CanSocket` lives in `av_can`, not `av_ipc`.** It is CAN-specific. But it
  *reuses* `av_ipc::UniqueFd` rather than growing a second Rule-of-5 fd wrapper,
  so `av_can` now depends on `av_ipc`. A duplicate would have been the easier
  edit and the worse design.
- **One signal table, included by both apps.** As two separate literals, a
  change on one side produces a plausible wrong number on the other — week 1's
  silent fault, reintroduced by copy-paste. It sits in `apps/`, not `libs/`,
  because week 6 replaces it with a parsed `.dbc` and it should not have to be
  deleted from the library then.
- **`WouldBlock` is not an error**, and `EINTR` folds into it. Both mean "come
  back later"; treating either as a failure is how a poll loop becomes a
  busy-wait or a spurious shutdown.
- **The receiver drains to `EAGAIN` on every wakeup** even though epoll is
  level-triggered and would forgive a partial read. Not for correctness — so a
  busy sender cannot starve the staleness check in the outer loop.
- **Staleness is a state the receiver computes**, not something the bus
  reports. Nothing arriving is a *fact*, and it is the only thing that
  separates a dead transmitter from a quiet one.

## 5. Hands-on exercise

`vcan` is not persistent, so this is needed once per boot:

```bash
sudo ./scripts/setup_vcan.sh          # creates vcan0
./check.sh                            # 10 test binaries, 4 sanitizers, tidy
```

Then, in three terminals:

```bash
./build/debug/apps/sim_vehicle/sim_vehicle vcan0     # 1: transmit
./build/debug/apps/can_rx/can_rx vcan0               # 2: receive and decode
candump -tz vcan0                                    # 3: the industry tool
```

Terminal 3 is the point of the exercise: `candump` and `can_rx` print the same
bytes. The output format of `can_rx` deliberately matches, so the two can be
compared line for line — which is how you settle "is the bug in my decoder or
on the bus?" in one glance.

Things to try:

1. **Kill `sim_vehicle`.** After 500 ms `can_rx` reports `signals are stale`.
   Restart it and watch `signals live again`. That transition, not the decode,
   is the part a cluster needs.
2. **`cansend vcan0 100#DEADBEEF`** — a 4-byte frame on an 8-byte message. The
   decoder reports the signals that no longer fit as *unavailable* rather than
   as zero. Week 1's rule, still holding.
3. **`cansend vcan0 300#0011223344556677`** — an id outside the filter set.
   `can_rx` never sees it; `candump` does. The kernel dropped it before it was
   copied.
4. **Comment out `set_filters`** and rerun with `cangen vcan0 -g 1`. Now every
   frame on the bus wakes the process.

## 6. Failure / debugging exercise

| Fault injected | Detected by | Logged as | Fallback | Safe state |
|---|---|---|---|---|
| Interface absent (post-reboot) | `if_nametoindex` returns 0 | `ERROR no such CAN interface hint=sudo ./scripts/setup_vcan.sh` | refuse to start | process exits 1, does not run blind |
| Transmitter stops | 500 ms without a frame | `WARN no frames -- signals are stale` | hold last value, mark invalid | staleness is visible to the UI |
| Frame shorter than the message | `decode` returns nullopt | `WARN signal unavailable` | signal dropped, others still decoded | absence != zero |
| Id outside the filter set | kernel, before any copy | — | frame never reaches the process | intended, not a fault |
| Empty filter set | **nothing** — the socket goes silent | nothing | none | test `AnEmptyFilterSetDeliversNothing` pins the behaviour |
| Controller degrades | `CAN_RAW_ERR_FILTER` | `ERROR bus error tec=.. rec=.. bus_off=..` | — | week 4's counters, finally readable |
| CAN-FD frame on a classic socket | **nothing** | nothing | none | see below |

### The silent fault of week 5

**A classic socket never sees an FD frame, and nothing anywhere says so.**

`CAN_RAW_FD_FRAMES` is off by default. If the transmitter sends CAN-FD and the
receiver has not enabled FD mode, the kernel drops the frame on the way in: no
error, no counter, no log line, no `EAGAIN` that means anything. From the
application's side, that ECU has simply stopped talking.

`candump` shows the frames arriving on the bus. The receiver shows nothing. The
gap between those two observations *is* the diagnosis — and it is only visible
because you thought to run both.

Pinned by `CanSocket.AClassicSocketNeverSeesAnFdFrame`.

Running record:

| Week | Fault | Why nothing catches it |
|---|---|---|
| 1 | wrong byte order | a plausible wrong number; CRC and ACK both pass |
| 2 | missing `close()` | no output at all; `join()` hangs forever |
| 3 | missing `release` store | correct on x86, wrong on ARM |
| 4 | node goes bus-off | the bus looks *healthier* than before |
| 5 | FD frame, classic socket | kernel drops it silently; `candump` sees it, you do not |

## 7. Senior interview questions

**1. What does `bind()` actually need for a CAN socket, and why does it fail so
often?**

A `struct sockaddr_can` carrying `can_family = AF_CAN` and a numeric
`can_ifindex`, obtained from `if_nametoindex()` or a `SIOCGIFINDEX` ioctl. It
fails because `vcan` is not persistent — it is gone after every reboot — and
because index 0 is legal and means *every* CAN interface, so a mistake there
gives you a receiver that quietly listens to the wrong bus rather than an error.

**2. Why must the identifier be masked on the way out of the kernel?**

`can_id` is not an identifier; it is an identifier plus three flags in the top
bits — `CAN_EFF_FLAG` (extended), `CAN_RTR_FLAG` (remote), `CAN_ERR_FLAG`
(error frame). Extended frames must be masked with `CAN_EFF_MASK`, standard
frames with `CAN_SFF_MASK`. Skip it and every extended id arrives with
`0x80000000` set, so every DBC lookup misses and the frames look unknown rather
than wrong.

**3. How do you tell a classic frame from an FD frame on the receive path?**

By the number of bytes `read()` returned: 16 for `struct can_frame`, 72 for
`struct canfd_frame`. There is no flag. And FD frames only arrive at all if
`CAN_RAW_FD_FRAMES` was set on the socket — otherwise the kernel silently drops
them.

**4. Where should CAN filtering happen, and why?**

In the kernel, via `CAN_RAW_FILTER`. A filter in the application still costs a
copy, a wakeup and a context switch per frame; on a 1000-frame-per-second bus
that is most of the receiver's CPU spent discarding data. The trap is that an
empty filter *list* means "deliver nothing", so a receiver that resets its
filters goes silent with no error.

**5. A signal stopped updating on the cluster. How does week 5 change your
first move?**

Run `candump` alongside the application. If the frames are on the bus and the
application does not have them, the fault is in the socket configuration —
filters, FD mode, wrong interface. If they are not on the bus either, the fault
is at or below the driver, and week 4's error counters are the next stop. That
single comparison halves the search space and takes ten seconds.

**6. How does a receiver distinguish "value is zero" from "no value"?**

It cannot, from the value alone — which is why it must not try. The receiver
tracks time-since-last-frame and reports staleness as a separate state.
`decode()` returning `nullopt` for a signal that does not fit, rather than 0, is
the same rule one layer down. A cockpit that shows 0 km/h because the bus went
quiet is worse than one that shows nothing.

**7. Why is it acceptable for `av_can` to depend on `av_ipc`?**

Because `UniqueFd` is exactly the abstraction needed, and duplicating it would
mean two Rule-of-5 implementations to keep correct. The dependency runs one way
only, from the CAN library to the generic IPC one, which is the direction that
makes sense: `av_ipc` knows nothing about vehicles.

## 8. Review checklist

- [x] I can explain this without notes
- [x] I implemented it, not just read it
- [ ] I broke it on purpose and watched it fail — **needs `sudo
      ./scripts/setup_vcan.sh` first**; the four exercises in §5 are the drill
- [x] I can defend the design trade-off I chose (§4)
- [x] Tests pass under `./check.sh` — debug / asan / ubsan / tsan / tidy clean,
      10 test binaries

**Honest status:** the 9 tests that need `vcan0` currently **skip**, because
creating the interface needs root and that has to be your keystroke, not mine.
Run the setup script and `./check.sh` again to turn them green. Until then week
5 is verified only as far as it can be without an interface.

### Left for later, deliberately

- **Reading `can-utils` source** (`candump.c`, `cansend.c`) → still worth doing;
  they are short, and `candump.c` is the reference for the format `can_rx`
  imitates.
- **Bit timing** (`ip link set can0 type can bitrate 500000 sample-point 0.875`)
  → matters on hardware, and `vcan` has none. Week 4 §8 has the concept.
- **`CAN_RAW_RECV_OWN_MSGS`** and `CAN_RAW_JOIN_FILTERS` → not needed until the
  capstone has two processes on one interface.
- **BCM sockets** (`CAN_BCM`, cyclic transmission in the kernel) → the right
  answer for a periodic transmitter in production; `sim_vehicle` uses a sleep
  loop because the sleep loop is the thing being taught.
