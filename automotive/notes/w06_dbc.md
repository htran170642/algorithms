# Week 6 — DBC: the contract that gives bytes meaning

## 1. Concept

A CAN frame carries **no self-description**. Eight bytes and a number:

```text
  vcan0  100   [8]  CC 1C 42 26 80 77 00 00
```

Nothing in that line says `CC 1C` is a speed, that it is little-endian, that it
must be multiplied by 0.01, or that the result is km/h. The **DBC file** is the
only thing that says so — and every ECU on the bus is expected to hold the same
copy of it.

```text
BO_ 256 EngineData: 8 ECU_Powertrain
 SG_ VehicleSpeed : 0|16@1+ (0.01,0) [0|655.35] "km/h" Cluster
    ^name          ^start     ^factor  ^min      ^unit  ^receiver
                     ^length     ^offset  ^max
                        ^byte order (1=Intel, 0=Motorola)
                         ^sign (+ unsigned, - signed)
```

Weeks 1 and 5 were both stand-ins for this. Week 1 built one `SignalSpec` by
hand; week 5 shared a hardcoded table between the transmitter and the receiver
through a common header. Week 6 deletes that header: both programs now read the
same `.dbc`, which is how a real project keeps two ECUs in agreement.

Three parts of the format beyond the layout itself:

| Row | Carries |
|---|---|
| `CM_` | free-text comments on a message or a signal |
| `VAL_` | an enumeration: raw value → human name (`1` → `"DriverOpen"`) |
| `[min\|max]` | the physical range — the field week 1 deliberately left out |

## 2. Why it exists in a Cockpit DC

- **It is the interface between organisations, not just between programs.** The
  DBC is written by whoever owns the bus and handed to every supplier. A
  cockpit team does not get to invent signal layouts; it gets a file.
- **It is the only defence against a plausible wrong number.** Week 1's silent
  fault — a wrong byte order producing a believable value — is *prevented* by
  both sides reading one document, and *caused* by them reading two.
- **Range enforcement is now possible.** `[min|max]` turns "the sensor is
  broken" from an invisible number into a logged fault, at the earliest layer
  that can tell.
- **It is what makes the receiver generic.** `can_rx` no longer contains a list
  of messages. Add a `BO_` to the file and it decodes it.

## 3. Architecture

```text
                     dbc/cockpit.dbc
                            │
             ┌──────────────┴──────────────┐
             │      one file, two readers   │
             ▼                              ▼
      sim_vehicle                       can_rx
     encode by name                 filters from the DBC
             │                       decode + range + VAL_
          write()                            ▲
             └────────► vcan0 ───────────────┘
```

The fork at the top is the part that matters. When those two branches read
*different* files, nothing below reports a problem.

## 4. C++ / Linux implementation

| File | Role |
|---|---|
| [dbc/cockpit.dbc](../dbc/cockpit.dbc) | the database: 2 messages, all 7 signals of CLAUDE.md §7 |
| [dbc.hpp](../libs/av_can/include/av/can/dbc.hpp) | `DbcSignal`, `DbcMessage`, `DbcDatabase` |
| [dbc.cpp](../libs/av_can/src/dbc.cpp) | the parser: `BO_`, `SG_`, `CM_`, `VAL_` |
| [dbc_test.cpp](../libs/av_can/tests/dbc_test.cpp) | 23 cases, including every malformed-input path |
| [sim_vehicle](../apps/sim_vehicle/main.cpp) · [can_rx](../apps/can_rx/main.cpp) | both rewritten to load the file |

Decisions worth defending:

- **`DbcSignal` exposes `decode`/`encode`, not just a `SignalSpec`.** A
  `SignalSpec` built from a `DbcSignal` borrows string_views from it, so letting
  one escape invites a dangling reference the day someone moves the database.
  Keeping the borrow inside the object removes the trap; `spec()` still exists,
  with the lifetime contract written down.
- **`encode` enforces the range; `decode` does not.** Deliberate asymmetry. A
  transmitter that cannot honour the DBC must not put the frame on the bus. A
  *receiver* that sees an out-of-range value is looking at a fact about the bus,
  and dropping it would hide the fault the range exists to reveal. `can_rx` logs
  it at ERROR and moves on.
- **`[0|0]` means "no range stated".** Many tools emit it. Enforcing it
  literally would reject every value except zero. The decision is made once, in
  the parser, so `in_range()` has no float comparison in it.
- **Multiplexed signals are rejected, loudly, with the line number.** Loading
  half a database would be worse: the missing signals would look like a quiet
  bus, which is the hardest symptom to diagnose.
- **The parser reports the line number on every failure.** A parser that cannot
  say *where* it failed is unusable on a 4000-line production file.
- **`apps/common/` was deleted.** Its own header said week 6 would replace it.
  Leaving it would have meant two sources of truth — which is precisely this
  week's silent fault, committed on purpose.

## 5. Hands-on exercise

```bash
./check.sh                                          # 11 test binaries, 4 sanitizers, tidy
sudo ./scripts/setup_vcan.sh                        # once per boot
./build/debug/apps/sim_vehicle/sim_vehicle vcan0    # terminal 1
./build/debug/apps/can_rx/can_rx vcan0              # terminal 2
candump -tz vcan0                                   # terminal 3
```

The receiver now prints names, not just numbers:

```text
INFO can.rx  signal  name=DoorStatus  value=1  means=DriverOpen
```

Things to try:

1. **Change `factor` for `VehicleSpeed` from `0.01` to `0.1` in the DBC, and
   restart only `can_rx`.** Speed reads 737.2 km/h. Nothing logs an error. See
   §6.
2. **Change `[0|655.35]` to `[0|50]` and restart `sim_vehicle`.** It refuses to
   transmit above 50 km/h: `encode refused ... min=0 max=50`. The range is
   enforced at the transmitter.
3. **Add `SG_ Extra : 48|8@1+ (1,0) [0|255] "" Cluster` to `EngineData`.**
   Restart `sim_vehicle`: it reports `no reading for signal` and stops sending
   that message, rather than transmitting a frame with a hole in it.
4. **Break a line on purpose** — `0-16@1+` instead of `0|16@1+`. The parser
   names the line number and refuses the file.

## 6. Failure / debugging exercise

| Fault injected | Detected by | Logged as | Fallback | Safe state |
|---|---|---|---|---|
| Malformed `SG_` line | parser | `ERROR malformed SG_ line line=17 text=...` | refuse the whole file | program exits, does not run half-configured |
| `SG_` before any `BO_` | parser | `ERROR SG_ before any BO_ line=4` | refuse | — |
| Multiplexed signal | parser | `ERROR multiplexed signals are not supported` | refuse | never loads a partial database |
| 11-bit id > 0x7FF | parser | `ERROR malformed BO_ line` | refuse | — |
| File missing | `ifstream` | `ERROR cannot open file path=...` | refuse | — |
| Value above `max` at the transmitter | `encode` | `ERROR encode refused ... min= max=` | message not sent | a wrong value never reaches the bus |
| Value above `max` at the receiver | `in_range` | `ERROR signal out of range` | signal not published | the fault is visible, not clamped away |
| Frame shorter than `BO_` says | length check | `WARN frame shorter than the DBC says` | fitting signals still decode | absence != zero |
| **Two DBC files that disagree** | **nothing** | **nothing** | **none** | see below |

### The silent fault of week 6

**The transmitter and the receiver hold different copies of the DBC.**

Change `factor` from `0.01` to `0.1` on one side only. Now:

```text
transmitter:  73.72 km/h  →  raw 7372  →  CC 1C
receiver:     CC 1C  →  raw 7372  →  737.2 km/h
```

Every layer below is perfect. Arbitration worked. CRC passed. ACK came back.
The bytes on the wire are byte-for-byte what the transmitter intended.
`candump` and `can_rx` print the same payload. No counter moves, no log line
appears, no test fails.

The cluster shows 737 km/h — and the only thing wrong is that two files
diverged.

This is week 1's fault grown up. Week 1's version was one program with a wrong
`ByteOrder`; this is two organisations with two revisions. The defences are
process, not code: **one file, version-controlled, distributed — never
retyped.** It is also why `apps/common/vehicle_signals.hpp` had to be deleted
rather than kept "just in case".

Running record:

| Week | Fault | Why nothing catches it |
|---|---|---|
| 1 | wrong byte order | a plausible wrong number; CRC and ACK both pass |
| 2 | missing `close()` | no output at all; `join()` hangs forever |
| 3 | missing `release` store | correct on x86, wrong on ARM |
| 4 | node goes bus-off | the bus looks *healthier* than before |
| 5 | FD frame, classic socket | kernel drops it silently; `candump` sees it, you do not |
| 6 | two DBC revisions | every layer is correct; only the meaning differs |

## 7. Senior interview questions

**1. What is in a DBC file, and what is not?**

Messages (`BO_`: id, name, length, transmitter), signals (`SG_`: start bit,
length, byte order, sign, factor, offset, range, unit, receivers), comments
(`CM_`), enumerations (`VAL_`), node names (`BU_`) and attributes (`BA_*`). What
is *not* in it: timing. A DBC does not say how often a message is sent — that is
an attribute by convention, or a separate document. It also says nothing about
arbitration, error handling, or the physical layer.

**2. `BO_ 2147483939` — what id is that?**

`0x80000123`. Bit 31 is the extended-frame flag, so this is the **29-bit id
0x123**, not a 2.1-billion id. A parser that reads it literally builds a message
nothing on the bus will ever match. It is the single most common DBC parsing
bug.

**3. Why does `encode` enforce the range but `decode` not?**

They face different directions. A transmitter that cannot honour the contract
must not send — better a missing frame than a wrong one. A receiver seeing an
out-of-range value has learned something true about the bus, and hiding it would
suppress exactly the fault the range was written to catch. So the receiver
decodes, checks separately, and logs.

**4. Two ECUs disagree about a signal's factor. What breaks, and what detects
it?**

Nothing breaks and nothing detects it. Arbitration, CRC, ACK, bit monitoring and
the error counters all pass — they protect the *bytes*, and the bytes are
correct. Only the interpretation differs. The defence is process: one
version-controlled file, distributed to every party, never retyped or
hand-edited per project.

**5. Why is `[min|max]` worth having when the bit width already bounds the
value?**

Because they say different things. 8 bits with offset −40 can *represent*
−40…215 °C; the DBC says the sensor is only valid over −40…100. A reading of
210 °C is representable, in range for the encoding, and physically impossible.
The bit width is a storage fact; the range is an engineering claim, and only the
second one can be violated meaningfully.

**6. How would you extend this parser for a real production file?**

Multiplexed signals first (`M` / `m<n>`), because they are common and currently
rejected. Then `BA_DEF_`/`BA_` attributes, which is where cycle time usually
lives. Then multi-line comments. And a signal-overlap check: nothing today stops
two `SG_` rows in one message from claiming the same bits — a real fault that a
DBC editor would catch and this parser would not.

**7. Where does the DBC sit relative to SOME/IP?**

They answer the same question at different layers. DBC maps *signals to bits in
a frame*; SOME/IP maps *methods and events to a serialised payload*. The move
from one to the other is the shift CLAUDE.md §5 describes — from `CAN ID →
message → signal` to `service → method/event/field`. Week 8 starts it.

## 8. Review checklist

- [x] I can explain this without notes
- [x] I implemented it, not just read it
- [x] I broke it on purpose and watched it fail — 8 fault paths in §6, all
      covered by tests; the four exercises in §5 are the manual drill
- [x] I can defend the design trade-off I chose (§4)
- [x] Tests pass under `./check.sh` — debug / asan / ubsan / tsan / tidy clean,
      11 test binaries, 23 of them DBC cases

**Carried over from week 5:** the 9 `vcan0` tests still skip until
`sudo ./scripts/setup_vcan.sh` has been run. The DBC work itself needs no
interface and is fully verified.

### Left for later, deliberately

- **Multiplexed signals** → rejected with a clear message today. Worth adding
  when a real file needs it, not before.
- **`BA_` attributes / cycle time** → week 10, where the middleware needs to
  know how often to expect a message in order to time out correctly.
- **Signal overlap detection** → a genuine gap: two `SG_` rows may claim the
  same bits and nothing complains. Belongs with the week-15 fault injection.
- **Writing DBC files** → not needed. This project consumes them.
