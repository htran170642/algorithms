# ROADMAP — 17 weeks to a Mini Cockpit Domain Controller

Execution tracker for [CLAUDE.md](CLAUDE.md).

**Calibration:** C++ is already solid; the automotive domain is new. Effort is
therefore shifted away from language fundamentals (CLAUDE.md §11 suggests 25%
for C++/Linux — this plan spends ~10%) and into CAN, SOME/IP and Qt.

**Rhythm:** ~10 h/week. 20 min theory · 55 min code · 15 min notes, six days.

**Rule (CLAUDE.md §13):** a week is done when the topic can be *explained,
implemented, debugged and defended* — not when the code compiles.

---

## Progress

| Wk | Topic | Artifact | Read real code | Note | Done |
|---:|---|---|---|---|:--:|
| 0 | Scaffold | CMake · GTest · 4 sanitizer presets · `av_log` · clang-tidy · `check.sh` | — | — | [x] |
| 1 | Bits + the spine | `av_can`: Intel/Motorola bit walk · `CanFrame` + CAN-FD DLC · `SignalSpec` decode/encode · `spine` demo with 2 injected faults | — | [`w01_spine.md`](notes/w01_spine.md) | [x] |
| 2 | Cockpit thread model | `BoundedQueue<T>` · rx/processing/UI threads · backpressure · graceful shutdown · TSan green | — | `w02_threads.md` | [ ] |
| 3 | Linux for CAN | `epoll` · non-blocking fd · Unix domain socket · **shared memory** · UDS-vs-shm latency benchmark | `can-utils`: `candump.c`, `cansend.c` | `w03_ipc.md` | [ ] |
| 4 | CAN fundamentals | arbitration simulator · error active/passive/bus-off state machine | — | `w04_can.md` | [ ] |
| 5 | SocketCAN | `sim_vehicle` → `vcan0` → C++ receiver over `PF_CAN` | kernel `net/can/raw.c` · `Documentation/networking/can.rst` | `w05_socketcan.md` | [ ] |
| 6 | CAN-FD + DBC | `.dbc` parser · all 7 signals of CLAUDE.md §7 decoded · BRS / 64-byte payload | — | `w06_dbc.md` | [ ] |
| 7 | Automotive Ethernet | UDP/multicast · **100BASE-T1 vs office Ethernet** · VLAN · QoS · PTP | — | `w07_ethernet.md` | [ ] |
| 8 | SOME/IP wire format | header · serialisation · request/response vs event · round-trip tests | `vsomeip` (COVESA) | `w08_someip.md` | [ ] |
| 9 | Service Discovery | OfferService / FindService · timeout · retry · availability | `vsomeip` SD | `w09_sd.md` | [ ] |
| 10 | **Middleware architecture** | design note: API boundary · ownership · thread model · failure model · backpressure/timeout/retry policy — then refactor `av_service` to match | — | `w10_middleware.md` | [ ] |
| 11 | Qt core | QObject · signals/slots · event loop · **demo: block the loop, freeze the UI, then fix it** | — | `w11_qt.md` | [ ] |
| 12 | C++ ↔ QML | `Q_PROPERTY` · `VehicleModel` · `QSocketNotifier` wiring the CAN fd into the event loop | Qt `QSocketNotifier` source | `w12_qml_bridge.md` | [ ] |
| 13 | Cluster UI | QML bindings · speed · RPM · temperature · doors · warnings | — | `w13_cluster.md` | [ ] |
| 14 | Capstone integration | full chain wired · **integration tests** separate from unit tests · end-to-end latency budget measured | — | `w14_capstone.md` | [ ] |
| 15 | Fault injection | CAN timeout · out-of-range · malformed frame · queue overflow · service unavailable — each following `Fault → Detection → Logging → Fallback → Safe State` | — | `w15_faults.md` | [ ] |
| 16 | OS / virtualisation / safety | QNX vs Linux · hypervisor isolation · AAOS + VHAL · ISO 26262 + ASIL · AUTOSAR Adaptive · **UDS / DoIP** | — | `w16_platform.md` | [ ] |
| 17 | Defence | **blind debug drill** (signal disappears, trace §9 unaided) · answer all of CLAUDE.md §10 | — | `w17_interview.md` | [ ] |

## Effort split

```text
Wk 0-3    foundations + IPC          4 wk   24%
Wk 4-6    CAN / CAN-FD               3 wk   18%
Wk 7-9    Ethernet / SOME-IP         3 wk   18%
Wk 10     middleware architecture    1 wk    6%
Wk 11-13  Qt / QML                   3 wk   18%
Wk 14-15  capstone (cross-cutting)   2 wk   12%
Wk 16-17  platform + interview       2 wk   12%
```

Weeks 16–17 sit below the 18% CLAUDE.md §11 suggests for Tier 2. That is
deliberate: it is reading, and it can be done in the gaps. Stretch to 18 weeks
if it feels thin.

## Deviations from CLAUDE.md, and why

| Deviation | Reason |
|---|---|
| Spine built in week 1, not week 16 | CLAUDE.md §14 asks for exactly this: *"intentionally small, then expanded"* |
| 4 weeks of C++17 fundamentals cut to ~1 | C++ is not the bottleneck; only bit/endianness survives, because it is what DBC decoding runs on |
| Lock-free SPSC queue deferred | A mutex-based queue is enough to reach the capstone; revisit after week 15 |
| UDS/DoIP added to week 16 | Present in CLAUDE.md §10 and §11 Tier 2 but missing from the week plan — a gap in the source document |
| 100BASE-T1/VLAN/QoS/PTP made explicit in week 7 | CLAUDE.md §3 W6 separates "Ethernet fundamentals" from "Automotive Ethernet"; the distinction is a standard interview question |
| Blind debug drill added (week 17) | Definition of Done #13 needs unaided tracing, which fault *injection* does not train |

## Definition of Done

The 15 items in CLAUDE.md §12. Tick them in `notes/w17_interview.md`, not here.

## Environment

```bash
./check.sh                        # debug + asan + ubsan + tsan + clang-tidy
./check.sh fast                   # debug only
./build/debug/apps/spine/spine    # week 1 demo
sudo ./scripts/setup_vcan.sh      # vcan0, from week 4
```

| Dependency | Status |
|---|---|
| g++ 13.3 · CMake 4.4 · Ninja · GTest 1.14 | installed |
| clang-tidy 18 | installed |
| Qt 6.11.1 | installed at `~/Qt/6.11.1/gcc_64` — set `CMAKE_PREFIX_PATH` in week 11 |
| `vcan` / `can-raw` kernel modules | present |
| `can-utils` | **missing** — `sudo apt install can-utils` before week 4 |
| `cppcheck`, `clang-format` | missing — optional, `check.sh` skips them |

TSan runs tests under `setarch -R`: kernel 6.x defaults to
`vm.mmap_rnd_bits=32`, which collides with ThreadSanitizer's fixed shadow
mapping. See [cmake/Sanitizers.cmake](cmake/Sanitizers.cmake).
