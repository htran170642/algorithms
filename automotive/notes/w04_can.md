# Week 4 — CAN fundamentals: arbitration and fault confinement

## 1. Concept

Weeks 1–3 all lived *above* the driver: a payload, a queue, a socket. This week
goes below it, to the two mechanisms a CAN controller runs in hardware and that
never appear in a `read()`:

| | question it answers | what it costs a node |
|---|---|---|
| **Arbitration** | who gets the wire when several ECUs start at once | nothing — the loser withdraws intact |
| **Fault confinement** | how a broken ECU is removed | error passive, then bus-off |

Two physical facts generate all of arbitration:

1. **The bus is wired-AND.** A dominant bit (0) actively pulls the line down; a
   recessive bit (1) merely lets it float. If any node drives dominant, every
   node reads dominant.
2. **Every transmitter reads back what it sent.** A node that sends recessive
   and reads dominant knows someone with a lower id is talking, stops
   immediately, and becomes a receiver.

That combination is why arbitration is **non-destructive**: the loser finds out
*before* it has put a single wrong bit on the wire, so the winner's frame is not
damaged, not delayed, and not retransmitted. Ethernet detects a collision after
the fact and throws both frames away. CAN resolves the collision without one
ever occurring.

The priority rule follows for free. The identifier goes out **most significant
bit first**, and 0 beats 1, so the numerically lower id wins. It is not a
convention someone chose — it falls out of the wiring.

### The two tie-breakers

Same id, different frames — the contest continues past the identifier:

| Position 11 | 12 | Outcome |
|---|---|---|
| data frame drives **RTR = 0**, remote drives **RTR = 1** | — | data beats remote: supplying a value outranks asking for one |
| standard drives **RTR**, extended must drive **SRR = 1** (always) | standard **IDE = 0**, extended **IDE = 1** | standard beats extended with the same base id — at 11 if the standard frame carries data, otherwise at 12 |

Position 11 is the interesting one. It is RTR for a standard frame and SRR for
an extended frame — the *same wire position*, two names — and the extended frame
is required to drive it recessive. It cannot win there.

### Fault confinement

Three states, driven by two counters (TEC for transmit, REC for receive):

```text
        TEC>127 or REC>127            TEC>=256
Active ────────────────────► Passive ──────────► Bus Off
   ▲                            │                   │
   └────────────────────────────┘                   │
        counters fall back        128 windows of    │
                                  11 recessive bits ┘
```

- **Error active** — normal. Signals an error with 6 **dominant** bits, which
  deliberately destroys the frame so every node discards it together.
- **Error passive** — still transmits and receives. Its error flag is 6
  **recessive** bits, which nobody notices, and it waits 8 extra recessive bits
  before starting a transmission, so it always loses the race for an idle bus.
- **Bus off** — the transmitter is disconnected. The node is electrically
  absent and the bus does not degrade at all.

Two asymmetries carry the entire design:

- **An error costs 8, a success refunds 1.** A node failing more than one frame
  in eight climbs toward bus-off instead of hovering. Intermittent faults are
  escalated, not tolerated.
- **The transmitter is charged 8, a receiver only 1.** When an error appears,
  the node that was talking is the likeliest cause. This is why one ECU with a
  bad transceiver removes *itself* rather than dragging every listener off with
  it.

Consequence worth saying out loud: **REC alone can never cause bus-off.** A
listen-only node tops out at error passive no matter how bad the bus gets.

## 2. Why it exists in a Cockpit DC

The cluster shares a bus with powertrain, brakes and body. Three things follow
directly from this week:

- **Priority is an id-assignment decision, made once, at design time.** Give the
  cluster's speed message a high id and it loses every contest against chatter
  from a body controller. There is no runtime knob to fix that later. Same
  reasoning as a thread priority — chosen by the architect, enforced by
  hardware.
- **A cockpit must survive a broken neighbour.** Fault confinement is why a
  failing ECU is a *missing signal*, not a dead bus. It is the CAN-level
  equivalent of the address-space isolation from week 3 and of the hypervisor's
  freedom from interference in week 13 — the same principle at three scales.
- **The counters are the evidence.** When a signal disappears, TEC/REC on the
  transmitting controller distinguish "the ECU is off the bus" from "the ECU is
  fine and nobody asked it for the value". CLAUDE.md §9 says do not assume the
  UI is broken; this week supplies the bottom two rows of that trace.

## 3. Architecture

```text
        Cockpit application               <- weeks 8-13
                 |
             Middleware                   <- weeks 8-10
                 |
             SocketCAN                    <- week 5
                 |
             CAN driver
                 |
   +-------------+-------------------+
   |      CAN controller             |    <- THIS WEEK
   |                                 |
   |   arbitration    error counters |
   |   (who talks)    (who stays)    |
   +-------------+-------------------+
                 |
          CAN transceiver
                 |
         CAN_H / CAN_L (wired-AND)
```

Everything in the boxed region is hardware. Software never sees it — which is
exactly why its failures reach the application disguised as something else.

## 4. C++ / Linux implementation

| File | Role |
|---|---|
| [arbitration.hpp](../libs/av_can/include/av/can/arbitration.hpp) | `Contender`, `BitValue`, `ArbitrationResult`; the field layout documented in one place |
| [arbitration.cpp](../libs/av_can/src/arbitration.cpp) | builds each node's arbitration field, then walks it bit by bit applying wired-AND |
| [error_state.hpp](../libs/av_can/include/av/can/error_state.hpp) | `ErrorCounters`, `ErrorEvent`, `ErrorState`, and the three thresholds |
| [error_state.cpp](../libs/av_can/src/error_state.cpp) | the counter rules, including the two special cases below |
| [can_bus/main.cpp](../apps/can_bus/main.cpp) | the demo: six scenarios, four of them faults |

Design decisions worth defending:

- **`Contender` is not a `CanFrame`.** Arbitration only ever sees the id, the
  format and RTR — and `CanFrame` carries no RTR, because SocketCAN reports it
  out of band. Modelling exactly what the arbitration field contains keeps the
  simulation from quietly depending on fields the bus never sees.
- **One bit-name table, in 29-bit numbering.** A standard frame calls positions
  0–10 `ID10..ID0` and an extended frame calls the same wire positions
  `ID28..ID18`. They are the same bits. Position 11 is genuinely dual-purpose,
  so it is labelled `RTR/SRR` — the only honest name.
- **The result carries a `winners` *vector*, not an index.** Two ECUs with the
  same id both survive arbitration. Returning one winner would have hidden a
  real configuration fault behind a plausible answer.
- **Bus-off latches.** Dropping TEC below 256 does not put a node back on the
  bus; only the 128-window recovery sequence does. Modelling that with a bool
  rather than a threshold test is what makes `BusOffFreezesTheCounters`
  expressible at all.
- **REC saturates at 256.** It can never cause bus-off, but a real controller's
  counter is only a few bits wide. Saturating stops a long fault from wrapping
  around into a healthy-looking value.

## 5. Hands-on exercise

```bash
./check.sh                              # debug + asan + ubsan + tsan + clang-tidy
./build/debug/apps/can_bus/can_bus      # the six scenarios
```

The first table is the one to stare at:

```text
  bit  name      BRAKE    ENGINE   BODY     INFO     bus
  0    ID28      0        0        0        1        0   <- INFO sent 1, read 0, withdraws
  1    ID27      0        0        1        .        0   <- BODY sent 1, read 0, withdraws
  2    ID26      0        1        .        .        0   <- ENGINE sent 1, read 0, withdraws
  bus granted winner=BRAKE bits=3
```

Three ECUs eliminated in three bits: no collision, no retry, no backoff.

## 6. Failure / debugging exercise

| Fault injected | Detected by | Logged as | Fallback | Safe state |
|---|---|---|---|---|
| Two ECUs share one id | neither — arbitration completes | `arbitration cannot resolve this still_transmitting=2` | none available at this layer | bus-level: both frames destroyed in the data field, both retry, sporadic error frames |
| Transceiver fails on transmit | controller TEC | `state change failed_frames=16 tec=128 state=passive` | node keeps transmitting, cannot signal errors | error passive |
| The same fault continues | controller TEC | `state change failed_frames=32 tec=256 state=bus-off` | node disconnects its transmitter | bus-off; bus unaffected |
| Bus goes quiet | 128 windows of 11 recessive bits | `recovered after a quiet bus windows=128 state=active` | counters cleared | error active |

### The silent fault of week 4

**A node goes bus-off and the bus gets *better*.**

Scenario 6 of the demo shows it. BRAKE is gone; ENGINE, BODY and INFO arbitrate
perfectly. Load is lower. Latency improves. Every remaining signal decodes.
`candump` shows a clean, healthy bus.

The cluster sees exactly one value stop updating — and every instinct points at
the wrong layer:

| Layer | What it reports |
|---|---|
| QML | binding fine, value stale |
| Qt model | no update received |
| Middleware | no event published |
| DBC decoder | never called |
| SocketCAN | no frame with that id — **and no error either** |
| Controller | TEC = 256, bus-off ← the actual evidence |

Nothing in the software stack is wrong. The evidence lives one layer *below* the
driver, in a counter no application ever reads. This is why "the signal is
missing" and "the bus is broken" are different diagnoses, and why the first
question is *where was the data last known to be correct* (CLAUDE.md §9).

Running record of silent faults:

| Week | Fault | Why nothing catches it |
|---|---|---|
| 1 | wrong byte order | a plausible wrong number; CRC and ACK both pass |
| 2 | missing `close()` | no output at all; `join()` hangs forever |
| 3 | missing `release` store | correct on x86, wrong on ARM |
| 4 | node goes bus-off | the bus looks *healthier* than before |

## 7. Senior interview questions

**1. Why is CAN arbitration non-destructive, and why does Ethernet need backoff?**

Because a CAN transmitter reads back every bit it sends on a wired-AND bus. A
node that sends recessive and reads dominant knows it has lost *at that bit* and
stops before contributing anything wrong. The winner's frame is untouched.
Ethernet's collision is detected after both stations have already corrupted the
medium, so both must discard and retry after a random delay. CAN pays for this
with a bit time long enough for a signal to reach the far end of the bus and
back — which is why CAN's bit rate is bounded by bus length.

**2. Why does a lower identifier have higher priority?**

The identifier is transmitted most significant bit first, and 0 (dominant)
overrides 1 (recessive). At the first bit where two ids differ, the one with 0
wins. "Lower value wins" is just the arithmetic consequence of MSB-first
transmission on a wired-AND bus.

**3. Standard and extended frame with the same base id — which wins?**

The standard frame, always. At wire position 11 the standard frame drives RTR;
for a data frame that is dominant, and the extended frame — which must drive SRR
recessive there — loses. If the standard frame is itself a remote frame, both
drive recessive and position 12 (IDE) decides: dominant for standard, recessive
for extended. Same outcome, one bit later.

**4. What actually happens when two ECUs are configured with the same id?**

Arbitration cannot resolve it. Both drive identical fields, both believe they
won, and they diverge in the data field — where the mismatch is a bit error, not
an arbitration loss. Error frames, retries, and TEC climbing on both nodes. The
symptom is sporadic corruption, never a priority complaint, which is why it
looks like a wiring problem.

**5. Can a node that only listens ever go bus-off?**

No. Bus-off is TEC ≥ 256, and a receiver only moves REC. The worst it reaches is
error passive. This is deliberate: the node most likely to be at fault is the
one transmitting, so it is charged 8 per error while listeners are charged 1.

**6. What is the practical difference between error active and error passive?**

Almost nothing an application can see, which is the danger. An error-passive
node still transmits and receives. It loses two things: its error flag is
recessive so nobody notices its complaints, and it waits 8 extra recessive bits
before starting a transmission, so it loses every race for an idle bus. The
symptom is intermittent latency and unreported corruption — not an outage.

**7. A signal stopped appearing on the cluster. Where do you look first?**

Not the UI. Establish where the data was last correct. If `candump` shows no
frame with that id, the problem is at or below the driver: check the
transmitting controller's error state. Bus-off is the clean answer; error
passive on a busy bus is the nastier one. If the frame *is* on the bus, the
fault is above the driver and the search moves to the DBC row, the decoder, and
the model binding.

**8. Why does an error cost 8 and a success only refund 1?**

To make the counters ratchet. At 1:1 a node failing half its frames would sit
near zero forever. At 8:1 anything worse than one failure in eight climbs
steadily to bus-off. The asymmetry converts "intermittently broken" — the
hardest state to diagnose — into "definitively removed".

## 8. Review checklist

- [x] I can explain this without notes
- [x] I implemented it, not just read it
- [x] I broke it on purpose and watched it fail (4 faults, §6)
- [x] I can defend the design trade-off I chose (§4)
- [x] Tests pass under `./check.sh` — debug, asan, ubsan, tsan, clang-tidy clean

Deferred on purpose:

- Bit stuffing, CRC, the ACK slot, and the error frame format itself. The
  controller handles them and they never reach software; they belong in a
  datasheet, not in a cockpit codebase.
- Bit timing (sync/prop/phase segments, sample point, SJW). It matters when
  commissioning a real bus and returns in week 5 with `ip link set can0 ...`,
  where the numbers are actually configurable.
