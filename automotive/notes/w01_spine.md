# Week 1 — Bits, CAN frames and the spine

> Vietnamese translation: [w01_spine_vi.md](w01_spine_vi.md). Keep both in step
> when this file changes.

## 1. Concept

A vehicle signal is not a number sitting in a message. It is a **rule** for
reading a run of bits:

```text
start_bit | length | byte order | signed? | factor | offset
```

Apply the rule to the payload and you get a raw integer; apply `factor` and
`offset` and you get something a human recognises. Reverse it to transmit.

The one genuinely hard part is byte order. DBC numbers bits LSB-first inside
each byte (bit 0 = byte 0 mask `0x01`, bit 8 = byte 1 mask `0x01`). Both
layouts share that numbering and differ only in the direction the signal walks:

| | `start_bit` is | walk |
|---|---|---|
| **Intel** (`@1`) | the signal's LSB | +1, straight through byte boundaries |
| **Motorola** (`@0`) | the signal's MSB | −1, except at a byte boundary where it is **+15** |

The +15 is the whole trick: leaving byte *k* at its bit 0 (number 8k) and
entering byte *k+1* at its bit 7 (number 8k+15) is a jump of exactly 15. That
single line in [next_bit()](../libs/av_can/src/bit.cpp) is the difference
between the two layouts.

DLC is the other thing that is not what it looks like. It is a 4-bit *code*,
not a byte count. Up to 8 they coincide; CAN-FD reuses codes 9–15 for 12, 16,
20, 24, 32, 48 and 64 bytes. So **a 13-byte CAN-FD payload does not exist** —
a real stack pads to 16 and the DBC declares the message as 16.

## 2. Why it exists in a Cockpit DC

Every number on the cluster starts here. Speed, RPM, coolant temperature, door
status — each one is a bit-range in a frame that a supplier wrote down in a DBC
and an ECU encodes at some fixed period.

Two consequences that matter later:

- **Byte order is not cosmetic.** Powertrain ECUs from European suppliers are
  overwhelmingly Motorola; body/comfort modules are often Intel. One cockpit
  decodes both, in the same process, from the same code.
- **A wrong byte order is silent.** Section 5 below demonstrates it: the same
  bytes read two ways give 82.5 km/h and 148.8 km/h, and both are plausible
  speeds. No CRC, no ACK, no error frame catches this. Only a plausibility
  check on the value, or a bench comparison against the real vehicle, will.

## 3. Architecture

Week 1 builds the two lowest boxes and stubs the rest:

```text
                            [ Cluster UI ]        week 13
                                  |
                            [ Qt model ]          week 12
                                  |
                            [ Middleware ]        weeks 8-10
                                  |
                        [ Vehicle data model ]    week 8
                                  |
    +-----------------------------+-----------------------------+
    |                     Signal decode                         |  <- HERE
    |     SignalSpec + factor/offset + sign  (av/can/signal)    |
    +-----------------------------+-----------------------------+
    |                       Bit walking                         |  <- HERE
    |          Intel / Motorola  (av/can/bit)                   |
    +-----------------------------+-----------------------------+
                                  |
                            [ CanFrame ]                           <- HERE
                                  |
                        [ SocketCAN / vcan0 ]     week 5
```

`av_can` is the library. It arrives in week 1 rather than week 4 because the
roadmap builds the spine first (CLAUDE.md §14); SocketCAN and a real DBC parser
are bolted onto the same library in weeks 5 and 6.

## 4. C++ / Linux implementation

| File | Role |
|---|---|
| [libs/av_can/include/av/can/bit.hpp](../libs/av_can/include/av/can/bit.hpp) | `ByteOrder`, `extract_bits`, `insert_bits`, `sign_extend` |
| [libs/av_can/src/bit.cpp](../libs/av_can/src/bit.cpp) | the walk; `next_bit()` is the whole Intel/Motorola difference |
| [libs/av_can/include/av/can/frame.hpp](../libs/av_can/include/av/can/frame.hpp) | `CanFrame`, DLC ↔ length, `is_valid` |
| [libs/av_can/src/frame.cpp](../libs/av_can/src/frame.cpp) | the sparse CAN-FD DLC table |
| [libs/av_can/include/av/can/signal.hpp](../libs/av_can/include/av/can/signal.hpp) | `SignalSpec` — one DBC `SG_` row |
| [libs/av_can/src/signal.cpp](../libs/av_can/src/signal.cpp) | `decode` / `encode`, factor/offset, range refusal |
| [apps/spine/main.cpp](../apps/spine/main.cpp) | the runnable demo, including two injected faults |

### Ownership and API decisions

- **`extract_bits` returns `std::optional`, not 0.** A signal that does not fit
  the frame is *not* a value of zero. Collapsing those two would let a
  truncated frame show 0 km/h on the cluster, which is a plausible reading and
  therefore the worst possible failure mode.
- **`insert_bits` validates the whole walk before writing a single byte.** A
  half-written signal decodes without error into a value that was never sent.
  Rejecting is strictly better than corrupting.
- **`encode` refuses out-of-range values rather than wrapping.** 700 km/h does
  not fit 16 bits at 0.01 resolution, so the transmitter says no.
- **No min/max in `SignalSpec` yet.** Range validation belongs with the DBC
  parser in week 6. A field that exists but is never enforced is worse than no
  field, because readers assume it is checked.

### Two undefined-behaviour traps this code steps around

Both are shift-related and both were deliberate:

```cpp
// 1. Shifting a 64-bit value by 64 is UB, and a 64-bit signal hits it exactly.
constexpr std::uint64_t low_mask(unsigned length) {
    return (length >= 64) ? ~std::uint64_t{0} : (std::uint64_t{1} << length) - 1U;
}

// 2. 2^63 - 1 is not representable as a double, so `scaled > 2^63 - 1` lets
//    through a value the int64 cast cannot take. Compare against 2^63 instead.
if (scaled < -limit || scaled >= limit) { return false; }
```

## 5. Hands-on exercise

```bash
./check.sh fast              # build + 4 test binaries
./build/debug/apps/spine/spine
```

The frame it produces:

```text
  vcan0  100   [8]  3A 20 0C 80 82 83 FF 00
                    \___/ \___/ \/ \___/
                     |     |     |   |
                     |     |     |   SteeringAngle -12.5 deg  (Intel, signed)
                     |     |     CoolantTemp 90 degC          (offset -40)
                     |     EngineRpm 800 rpm                  (Motorola!)
                     VehicleSpeed 82.5 km/h                   (Intel)
```

Worth checking by hand once, because it is the last time the numbers will be
small enough to: `82.5 / 0.01 = 8250 = 0x203A`, Intel so low byte first →
`3A 20`. `800 / 0.25 = 3200 = 0x0C80`, Motorola so high byte first → `0C 80`.

The `vcan0 100 [8] ...` format is deliberately `candump`'s, so this output and
the real tool's line up in week 5.

## 6. Failure / debugging exercise

| Fault injected | Detected by | Logged as | Fallback | Safe state |
|---|---|---|---|---|
| Truncated frame (8 → 2 bytes) | `extract_bits` walk leaves the payload | `WARN signal unavailable name=EngineRpm reason=does not fit frame` | none yet — value simply absent | week 8: hold last value, then mark stale after a timeout |
| Wrong byte order in the DBC row | **nothing** | `INFO intel=82.5 motorola=148.8` | none possible | only a plausibility/rate-of-change check can catch it |
| Out-of-range encode (700 km/h) | `encode` range test | caller gets `false` | frame left untouched, nothing transmitted | transmitter refuses rather than wrapping to 44.6 km/h |
| Non-encodable CAN-FD length (13 bytes) | `is_valid` via `length_to_dlc` | caller gets `false` | frame not sent | — |

The second row is the important one. It is the first example in this project of
a fault with **no detection mechanism at the layer where it happens**. CRC, ACK
and error frames all pass: the bus delivered exactly the bytes that were sent.
The defect is in the *interpretation*, and the only defence is higher up —
range limits, rate-of-change limits, or a bench comparison.

That is also the honest answer to "why does functional safety care about more
than a checksum" (week 14).

## 7. Senior interview questions

**1. A DBC row says `SG_ EngineRpm : 23|16@0+ (0.25,0)`. Which bytes does it
occupy and why?**

Bytes 2 and 3, big-endian. Start bit 23 is byte 2 bit 7 (23 = 2×8 + 7), the
signal's MSB. The walk goes 23→16 down byte 2, then the +15 jump lands on bit
31 = byte 3 bit 7, and continues 31→24. So `raw = (data[2] << 8) | data[3]`,
and the physical value is `raw × 0.25` rpm.

**2. Your cluster shows 148.8 km/h when the vehicle is doing 82.5. Where do you
look first?**

The DBC row, not the UI. That ratio is the byte-swap signature: `0x203A` read
Intel is 8250, read Motorola it is `0x3A20` = 14880. Anything where the two
readings are related by a byte swap points at `@0` vs `@1`, not at the bus, the
driver, or the binding. Confirm by hand-decoding one raw frame from `candump`.

**3. Why does `extract_bits` return `std::optional` instead of just zero?**

Because "not received" and "received as zero" must reach the cluster as
different things. A truncated frame that decodes to 0 km/h is indistinguishable
from a stationary car. The optional forces every caller to make that decision
explicitly, and it is the same distinction the middleware needs in week 8 to
implement staleness and safe-state fallback.

**4. Why can't a CAN-FD frame carry 13 bytes?**

DLC is a 4-bit code, not a length. Codes 0–8 are the byte count; CAN-FD reuses
9–15 for the jump to 12/16/20/24/32/48/64. There is no code for 13, so the
transmitter pads to 16 and the DBC declares 16.

**5. `sign_extend` casts a `uint64_t` with the top bit set to `int64_t`. Is
that undefined?**

Not undefined — *implementation-defined* in C++17 (well-defined from C++20,
which mandates two's complement). Every target this project runs on is two's
complement, and the comment says so. Worth flagging in review rather than
leaving silent: the same pattern is a genuine portability bug on a
sign-magnitude machine, which is why C++20 removed the ambiguity.

## 8. Review checklist

- [x] I can explain this without notes
- [x] I implemented it, not just read it
- [x] I broke it on purpose and watched it fail — truncated frame, swapped byte
      order, out-of-range encode
- [x] I can defend the design trade-off I chose — `optional` over sentinel zero,
      refuse over wrap, validate-before-write
- [x] Tests pass under `./check.sh` — debug / asan / ubsan / tsan / clang-tidy clean

### Left for later, deliberately

- Range (`[min|max]`) validation → week 6, with the DBC parser
- Multiplexed signals (`SG_MUL_VAL_`) → week 6
- Bit-by-bit walking is O(n) per signal; a production decoder uses byte-wise
  masks. Correctness first, and 64 iterations is not the bottleneck at 100 Hz.
