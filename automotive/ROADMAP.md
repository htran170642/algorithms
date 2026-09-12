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
| 1 | Bits + the spine | `av_can`: Intel/Motorola bit walk · `CanFrame` + CAN-FD DLC · `SignalSpec` decode/encode · `spine` demo with 2 injected faults | — | [`w01_spine.md`](notes/w01_spine.md) · [vi](notes/w01_spine_vi.md) · [code](notes/w01_code_walkthrough.md) | [x] |
| 2 | Cockpit thread model | `av_conc`: `BoundedQueue<T>` (ring buffer, close-then-drain) · `cockpit` demo: rx/decode/ui threads, drop-oldest backpressure, ordered shutdown · TSan green | — | [`w02_threads.md`](notes/w02_threads.md) · [vi](notes/w02_threads_vi.md) | [x] |
| 3 | Linux IPC | `av_ipc`: `UniqueFd` · `UnixSocket` (SEQPACKET) · `Poller` (epoll) · `SharedRing` (shm, SPSC lock-free) · `ipc_bench`: **10x** median, 12x p99 | deferred to wk 5 | [`w03_ipc.md`](notes/w03_ipc.md) · [vi](notes/w03_ipc_vi.md) | [x] |
| 4 | CAN fundamentals | `av_can`: bit-by-bit arbitration simulator (wired-AND, RTR/SRR/IDE tie-breakers) · `ErrorCounters` active/passive/bus-off + recovery · `can_bus` demo: 6 scenarios, 4 faults | — | [`w04_can.md`](notes/w04_can.md) · [vi](notes/w04_can_vi.md) · [nền tảng](notes/w04_can_basics_vi.md) | [x] |
| 5 | SocketCAN | `av_can`: `CanSocket` (`PF_CAN`, ifindex bind, kernel filters, FD mode, `CAN_RAW_ERR_FILTER` → week 4's TEC/REC) · `sim_vehicle` 10 Hz → `vcan0` → `can_rx` (week-3 epoll + week-1 decode + staleness) | deferred: `candump.c` | [`w05_socketcan.md`](notes/w05_socketcan.md) · [vi](notes/w05_socketcan_vi.md) | [~] |
| 6 | CAN-FD + DBC | `av_can`: `DbcDatabase` parser (`BO_`/`SG_`/`CM_`/`VAL_`, bit-31 extended flag, `[min\|max]` enforced) · [`dbc/cockpit.dbc`](dbc/cockpit.dbc) with all 7 signals of CLAUDE.md §7 · `apps/common/` deleted — both apps read the one file | — | [`w06_dbc.md`](notes/w06_dbc.md) · [đọc file dbc](notes/w06_dbc_file_vi.md) | [x] |
| 7 | Automotive Ethernet | `av_eth`: `UdpSocket` (per-NIC IGMP join, TTL, DSCP, refuses >1472) · `av_can`: 16-byte tunnel header + wrap-safe `SequenceTracker` · `eth_gw --reorder` → `eth_sub` vs `eth_sub --naive`: **identical datagrams, speed climbs vs walks backwards** · 100BASE-T1 / VLAN / QoS / PTP documented, not implemented | — | [`w07_ethernet.md`](notes/w07_ethernet.md) · [vi](notes/w07_ethernet_vi.md) | [x] |
| 8 | SOME/IP wire format | `av_service`: 16-byte header (Length counts from Request ID, big-endian) · `PayloadWriter/Reader` · `SessionCounter` wrapping to 1 · byte-pinned tests that caught a real offset bug a round-trip would have hidden · `svc_server`/`svc_client`: **method (unicast, echoes Request ID) vs event (multicast, nobody asked)** + `E_UNKNOWN_METHOD`, version mismatch, 300 ms timeout | deferred: `vsomeip` | [`w08_someip.md`](notes/w08_someip.md) · [vi](notes/w08_someip_vi.md) | [x] |
| 9 | Service Discovery | `av_service`: SD entries + IPv4 options (Length declares 9, option occupies 12) · `ServiceRegistry` treating an offer as a **lease**, clock injected so expiry is tested in µs · `av_eth`: `leave_multicast` · `svc_server` offers cyclically, answers `FindService`, sends `StopOffer` on Ctrl-C · `svc_client` **starts before the server**, searches, learns both endpoints, joins the event group only once told: **StopOffer = 0 failed calls, `kill -9` = 2 calls and 3 s** · reboot detection, and the loopback bug the run found | deferred: `vsomeip` | [`w09_sd.md`](notes/w09_sd.md) · [vi](notes/w09_sd_vi.md) | [x] |
| 10 | **Middleware architecture** | `av_mw`: `Runtime` owns **no thread** (`poll()` / `fd()`, pimpl) · `Proxy`: every call ends exactly once, never inside `call()`, no method retry (idempotency), `Busy` past 16, `NotAvailable` at once on withdrawal · `Skeleton`: offer ≠ construct, StopOffer in the destructor · `VehicleProxy/Skeleton` = what codegen emits · apps **711→216 / 492→139 lines**, link `av_mw` only, sockets unreachable · 10 integration tests on one thread · measured: unicast to an `SO_REUSEADDR` port reaches only the last-bound socket | deferred: `vsomeip` | [`w10_middleware.md`](notes/w10_middleware.md) · [vi](notes/w10_middleware_vi.md) | [x] |
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
./build/debug/apps/spine/spine      # week 1 demo
./build/debug/apps/cockpit/cockpit  # week 2 demo
./build/debug/apps/ipc_bench/ipc_bench  # week 3 benchmark
./build/debug/apps/can_bus/can_bus      # week 4 arbitration + bus-off demo

sudo ./scripts/setup_vcan.sh                       # vcan0 — REQUIRED from week 5, once per boot
./build/debug/apps/sim_vehicle/sim_vehicle vcan0    # week 5 transmitter
./build/debug/apps/can_rx/can_rx vcan0             # week 5 receiver
candump -tz vcan0                                  # compare against the industry tool
```

| Dependency | Status |
|---|---|
| g++ 13.3 · CMake 4.4 · Ninja · GTest 1.14 | installed |
| clang-tidy 18 | installed |
| Qt 6.11.1 | installed at `~/Qt/6.11.1/gcc_64` — set `CMAKE_PREFIX_PATH` in week 11 |
| `vcan` / `can-raw` kernel modules | present |
| `can-utils` | installed — `candump`, `cansend`, `cangen` in `/usr/bin` |
| `cppcheck`, `clang-format` | missing — optional, `check.sh` skips them |

TSan runs tests under `setarch -R`: kernel 6.x defaults to
`vm.mmap_rnd_bits=32`, which collides with ThreadSanitizer's fixed shadow
mapping. See [cmake/Sanitizers.cmake](cmake/Sanitizers.cmake).
