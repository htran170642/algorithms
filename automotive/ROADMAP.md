# Automotive C++ / CDC Roadmap — 33 Weeks (27 CORE + 6 EXT)

**Chuẩn:** C++17 baseline + C++20 build matrix. Mọi artifact phải build và pass dưới **cả hai**.
**Nguồn:** [CLAUDE.md](CLAUDE.md) là giáo trình; file này là bộ theo dõi.

**CORE** = đủ để đạt cả 23 mục ở [CLAUDE.md §11 Definition of Done](CLAUDE.md) (~27 tuần ≈ 6.2 tháng
@13h/tuần, đúng mục tiêu 6 tháng của §3).
**EXT** = đáng học nhưng bỏ được nếu cần đi phỏng vấn sớm.
**[credit]** = đã chứng minh bằng artifact ở `cplus_review`; chỉ làm phần delta automotive.

---

## Một tuần được coi là XONG khi đủ **7** điều

1. Artifact build được dưới **cả C++17 và C++20**
2. Test xanh dưới `debug` + `asan` + `ubsan` (+ `tsan` nếu có concurrency)
3. Cổng `clang-tidy` xanh
4. Note đã viết ở `notes/wNN_*.md`, **mở đầu bằng bảng Bằng chứng**
5. **Recall check** — giải thích lại khái niệm bằng miệng, không mở note (§5 bước 2)
6. Câu hỏi phỏng vấn đã thêm vào `docs/interview/`; challenge đã commit
7. Checkbox dưới đây được tick **kèm số đo**, không phải chữ "xong"

> Quy tắc: không có số đo thì không được tick. `"pool 2055 ns vs new/delete 13273 ns ≈ 6.5×"` là
> một entry hợp lệ. `"đã hiểu allocator"` thì không.

---

## Tiến độ

- [x] **W0** — Substrate
- [x] **W1** — No-heap discipline
- [ ] W2 · W3 — Embedded C++
- [ ] W4–W7 — Linux / POSIX / RTOS
- [ ] W8–W11 — Automotive Networking
- [ ] W12–W14 — CDC / Hypervisor / AAOS
- [ ] W15–W18 — Cockpit / HMI
- [ ] W19–W21 — AUTOSAR
- [ ] W22–W25 — Build / Embedded Linux / OTA
- [ ] W26–W28 — Testing / Process / Safety
- [ ] W29–W32 — Capstone Hardening

---

# Phase 0 — Substrate

- [x] **W0 · CORE** — Preset matrix `{debug,asan,ubsan,tsan,release}` × `{-17,-20}` = 10 preset ·
  `cmake/{Warnings,Sanitizers}.cmake` · `check.sh` · `av_add_test()` · `libs/` vs `phases/` split.
  **Đo được:** `./check.sh` = ALL GREEN 6/6 preset. GoogleTest v1.15.2 (1.14.0 không configure được
  dưới CMake 4.4). TSan chạy dưới `setarch -R` để tránh va shadow-mapping với ASLR kernel 7.0.
  Hai tầng warning: tầng strict (`-Wold-style-cast`, `-Wfloat-equal`) chỉ áp cho `libs/` vì macro
  GoogleTest tự nó sinh C-style cast.
  - [ ] Nợ: cài `clang-tidy` + `valgrind` (`sudo apt install clang-tidy valgrind`)
  - [ ] Nợ: `git add` thư mục này — chưa track thì cổng clang-tidy im lặng pass trên danh sách rỗng

---

# Phase 1 — Embedded C++ (W1–W3)

- [~] **W1 · CORE [credit, ~2d] — No-heap discipline.** *Credit: RAII, Rule 0/3/5, move, smart
  pointers, pool allocator, alignment/padding (cplus_review W1–W9, W19, W27–W31).*
  Artifact `libs/av_core/include/av/fixed_vector.hpp`. Xanh 6/6 preset.
  **Đo được:** `sizeof(std::vector<int>)` = 24 B *bất kể nội dung* vs `FixedVector<int,1024>` =
  **4104 B kể cả khi rỗng** — trade-off định lượng được. `&v[0]` nằm trong `[&v, &v+sizeof(v))` →
  bằng chứng no-heap kiểu **structural**, mạnh hơn và bền hơn việc đếm `malloc` (không vỡ dưới ASan).
  ⭐ `MoveIsLinearNotConstant`: `Tracker::moves == 3` — **move O(n), không phải O(1)**; `std::vector`
  ở đây sẽ là 0. ⭐ `ThrowingCopyLeavesNothingBehind`: `Bomb::live` về đúng 4 → constructor ném thì
  `~FixedVector()` **không chạy**, phải rollback tay trong `catch`. AUTOSAR A5-2-4 cấm
  `reinterpret_cast` → dùng 2 `static_cast` qua `void*` + `std::launder`.
  ⭐ Bẫy: preprocessor chạy **trước** parser nên không hiểu template — dấu phẩy trong `Foo<int,4>`
  làm vỡ macro GoogleTest; chữa bằng `using` alias.
  **`FixedString<N>`** (11 test): invariant `strlen(c_str()) == size()` giữ được sau **mọi** mutation
  — đúng thứ `strncpy` KHÔNG giữ (`strncpy(d,"hello world",8)` → `"hello wo"` **không có `'\0'`**).
  ⭐ `sizeof(FixedString<8>)` = **24 B chứ không phải 17** → padding vì `char buf_[9]` bị đệm lên 16
  cho `size_t` đúng biên. Hai chính sách cắt tách bạch: `try_append` (all-or-nothing, cho path/ID) vs
  `append_truncating` (ghi phần vừa + trả `false`, cho log) — **cắt im lặng mới là bug**.
  ⭐ Hidden friend idiom để `fs == "abc"` chạy được (free function template sẽ fail deduction).
  Rule of Zero: `char[]` trivially copyable ⇒ copy/move do compiler sinh vẫn giữ invariant.
  → `notes/w01_no_heap.md`, `docs/interview/cpp_memory.md` (10 câu)
- [~] **W2 · CORE [credit, ~2d] — RT-safe concurrency.** *Credit: thread, cv, atomic, memory_order,
  ThreadPool, BlockingQueue, SpscRing (cplus_review W32–W38).* Delta: priority inversion.
  Artifact `phases/phase01_rt_concurrency/` — build xanh 6/6 preset.
  ⚠️ **Số đo còn TRỐNG**: máy dev có `ulimit -r = 0` ⇒ `SCHED_FIFO` bị từ chối ⇒ 3 test `SKIPPED`
  (skip có kèm cách cấp quyền, **không** âm thầm pass). Chưa tick được vì chưa có số.
  Nội dung đã chốt: **bounded** (chờ ≤ critical section, tính được) vs ⭐ **unbounded** (TRUNG BÌNH
  giành CPU của THẤP ⇒ cận trên phụ thuộc thread *không liên quan* ⇒ WCET sụp) — **cùng bệnh với
  `malloc` ở W1: không phải chậm, mà là không có cận trên**. Mars Pathfinder 1997: JPL bật
  `PTHREAD_PRIO_INHERIT` bằng patch upload từ Trái Đất. POSIX mặc định là `PTHREAD_PRIO_NONE` = TẮT.
  ⭐ Bẫy: thiếu `PTHREAD_EXPLICIT_SCHED` thì policy/priority **bị bỏ qua im lặng** (mặc định
  `PTHREAD_INHERIT_SCHED`). Phải ghim 1 CPU, `busy_for` phải quay CPU thật (sleep là hỏng),
  `CLOCK_MONOTONIC` không phải `CLOCK_REALTIME`. ⭐ Bẫy CMake: `Threads::Threads` cần
  `find_package(Threads)` tường minh — dựa vào lời gọi nội bộ của GoogleTest là sai.
  → `notes/w02_priority_inversion.md`
  - [ ] Nợ: chạy `sudo ./build/debug-17/phases/phase01_rt_concurrency/phase01_priority_inversion_test`
        rồi điền bảng Bằng chứng
  - [ ] Nợ: `docs/interview/cpp_concurrency.md`
- [ ] **W3 · CORE [FULL] — Embedded C++.** `volatile` (và **vì sao nó KHÔNG atomic**) · bit
  manipulation · MMIO · endianness · packed struct vs bitfield cho wire format · linker section · ABI.
  Gộp luôn delta performance và cổng coding-standard.
  → `av_core`: `bit.hpp`, `endian.hpp`, `mmio.hpp`, `clock.hpp` + `.clang-tidy` (MISRA/AUTOSAR C++14)
  + `docs/coding_standard.md`

---

# Phase 2 — Linux / POSIX / RTOS (W4–W7)

- [ ] **W4 · CORE — IPC.** Unix domain socket · shared memory · `eventfd` · message queue.
  → `libs/av_ipc` + `libs/av_signal` (**mô hình vehicle signal**: VehicleSpeed, EngineRPM,
  CoolantTemperature, FuelLevel, BatteryVoltage, DoorStatus, TurnSignal — kèm validity, timestamp,
  range). ⭐ **Mốc capstone đầu tiên: 2 tiến trình trao đổi một signal sống.**
- [ ] **W5 · CORE — epoll + reactor.** `select`→`poll`→`epoll` · edge vs level triggered.
  → event loop mà mọi service sẽ chạy trên đó
- [ ] **W6 · CORE — Real-time + nguồn thời gian** (§2.6 phần 1). `SCHED_FIFO`/`RR` · CPU affinity ·
  jitter · **WCET** · PREEMPT_RT · `CLOCK_MONOTONIC` vs `CLOCK_REALTIME` · timestamping · vì sao
  wall-clock trong log xe là bug. → báo cáo jitter đo được của đường IPC W4
- [ ] **W7 · CORE — QNX / VxWorks.** Microkernel vs monolithic · message passing · channel/connection
  · resource manager. → shim `MsgSend`/`MsgReceive`/`MsgReply` kiểu QNX trên UDS; phần còn lại là
  design doc

---

# Phase 3 — Automotive Networking (W8–W11)

- [ ] **W8 · CORE — CAN.** Frame · identifier · DLC · CRC · ACK · **arbitration** · bit stuffing ·
  error active/passive · **bus-off**. Cài `can-utils`, dựng `vcan0`. → `libs/av_can`
- [ ] **W9 · CORE — DBC + CAN-FD + LIN.** Signal decoding (start bit, length, byte order, scale,
  offset) · BRS · LIN master/slave/schedule/checksum. → **`apps/can-service`**
- [ ] **W10 · CORE — Automotive Ethernet + SOME/IP + time sync** (§2.6 phần 2). 100BASE-T1 ·
  SOME/IP header · Service Discovery qua UDP multicast · **PTP/gPTP (802.1AS)** · TSN shaper · DDS
  (Tier C awareness). → `libs/av_someip` → **`apps/network-service`**
- [ ] **W11 · CORE — UDS / DoIP** (§2.3). Session · RDBI · DTC read/clear · ECU reset · Security
  Access · Routine Control · UART/SPI/I²C. → `libs/av_uds` → **`apps/diagnostic-service`**

---

# Phase 4 — CDC / Hypervisor / AAOS (W12–W14) — thiên về thiết kế, không cần phần cứng

- [ ] **W12 · CORE — CDC architecture.** Domain consolidation · SoC · mixed criticality ·
  **freedom from interference**. → `docs/architecture/cdc.md`: stack HW→hypervisor→QNX/Linux/Android
- [ ] **W13 · CORE — Hypervisor.** Type 1 · CPU/memory/device virtualization · IOMMU · partitioning ·
  GPU passthrough. → **thay thế hands-on:** safety domain vs infotainment domain = 2 Docker/QEMU
  domain + virtual network + shared-memory channel
- [ ] **W14 · CORE — AAOS.** Binder · AIDL · HAL · **VHAL** · Car Service. → map mô hình signal W4
  sang VHAL property ID

---

# Phase 5 — Cockpit / HMI (W15–W18)

- [ ] **W15 · CORE [reduced] — Qt C++ delta.** *Credit: signals/slots, QThread (`1_qt` ch2, ch4).*
  Delta: **thread affinity qua ranh giới IPC** · Model/View trên signal sống
- [ ] **W16 · CORE — QML dashboard.** → **`apps/hmi`**: speed, RPM, fuel, battery, temp, warnings
- [ ] **W17 · CORE — Qt + C++ architecture.** QML → Controller → VehicleService → IPC · backend
  abstraction để HMI test được headless
- [ ] **W18 · EXT — Display & multimedia pipeline** (§2.7). Một frame QML đi tới màn hình bằng cách
  nào: **Wayland → DRM/KMS → hardware compositor** · EGL/OpenGL ES · zero-copy (dmabuf) · video &
  camera pipeline. **Tận dụng kinh nghiệm GStreamer/DeepStream sẵn có.** → camera surface ghép vào
  dashboard

---

# Phase 6 — AUTOSAR (W19–W21) — thiên Adaptive theo CLAUDE.md

- [ ] **W19 · CORE [3d] — Classic.** App → RTE → BSW → MCAL · SWC · COM · OS · DEM · DCM
- [ ] **W20 · CORE — Adaptive.** Execution Mgmt · Communication Mgmt · Persistency · Diagnostics ·
  **UCM** · Time Sync · State Mgmt — ⭐ **map từng cái vào thứ mình đã xây** (`network-service` ≈
  `ara::com`, `diagnostic-service` ≈ `ara::diag`)
- [ ] **W21 · EXT — SOA deepening.** Nắn interface capstone theo dáng `ara::com`

---

# Phase 7 — Build / Embedded Linux / OTA (W22–W25)

- [ ] **W22 · CORE [reduced, 3d] — CMake.** `install`/`export`/toolchain file + **cross-compile
  aarch64**. *Credit: chính cái substrate này là bài học CMake.*
- [ ] **W23 · CORE — Yocto trong Docker.** BitBake · recipe · layer · image · machine · distro · SDK ·
  poky + meta-qt6 · app thành recipe kèm systemd service
- [ ] **W24 · CORE — Boot / BSP / Device Tree** (§2.4). U-Boot · boot sequence · kernel startup ·
  Device Tree · sysroot · ABI. *Sub-task EXT:* char driver với `ioctl` + `mmap`
  *(nâng cấp phần cứng tuỳ chọn: Raspberry Pi làm phần này thành thật thay vì QEMU)*
- [ ] **W25 · EXT — OTA & Software Update** (§2.5). **A/B partitioning** · rollback · update package ·
  version management · secure update · failure recovery · SWUM / AUTOSAR **UCM**.
  → mô phỏng A/B update + rollback trên image W23; trả lời design question *"Design an OTA update
  mechanism"*

---

# Phase 8 — Testing / Process / Safety (W26–W28)

- [ ] **W26 · CORE [reduced] — Testing.** GoogleMock + **harness integration đa tiến trình** · HIL
  concepts. *Credit: kỷ luật unit test.*
- [ ] **W27 · EXT — A-SPICE.** Requirements → architecture → design → unit/integration/system
  verification · **ma trận traceability** cho capstone · configuration/change/defect management ·
  CAN tooling mã nguồn mở thay CANoe (trace, signal, message, DBC, CAPL)
- [ ] **W28 · CORE — Safety + Security.** **ISO 26262**: HARA cho capstone · ASIL A–D/QM · safety
  goal · FSC/TSC · safe state · fail-operational. **ISO/SAE 21434, UNECE R155/R156**: TARA · secure
  boot · HSM · key management · **TLS/PKI/certificate lifecycle** · secure diagnostics.
  → hiện thực safety mechanism: invalid-signal detection, timeout, last-valid-value, safe fallback,
  fault state

---

# Phase 9 — Capstone Hardening (W29–W32)

- [ ] **W29 · EXT — Observability** (§8). Structured log · metrics · latency measurement · health
  status · watchdog · systemd supervision · tracing · resource monitoring
- [ ] **W30 · CORE — Performance.** Đo đủ 8 mục §8: latency · throughput · CPU · memory ·
  **allocation count** · IPC overhead · queue depth · dropped messages
- [ ] **W31 · EXT — Reliability.** Fault injection theo §8: timeout · malformed message ·
  disconnected process · service restart · invalid CAN signal · queue overflow · network failure ·
  process crash
- [ ] **W32 · CORE — Final integration.** Architecture doc · demo end-to-end · **quét đủ 23 mục
  §11 Definition of Done**

---

# Cross-Cutting — làm hằng tuần, không phải tuần riêng

- **Design rounds** — 1 vòng mỗi ranh giới phase, **8 vòng**, ở `docs/design/`. Đề bài lấy thẳng từ
  [CLAUDE.md §6](CLAUDE.md): IVI architecture · CDC platform · QNX↔Linux communication · vehicle data
  service · diagnostic service · **OTA update mechanism** · fault-tolerant speed-signal pipeline.
- **Mock interviews** — ở ranh giới phase, `docs/mocks/`
- **Concept Q&A** — bổ sung `docs/interview/` mỗi tuần
- **Challenges** — ~2/tuần ở `challenges/`, ưu tiên thứ `cplus_review` **chưa** làm:
  **serialization, wire-format parsing, networking**
- **Tỷ lệ tuần** (§5): 30% lý thuyết · 40% code · 20% project · 10% luyện phỏng vấn

---

# Phải tự viết lại được từ đầu, không nhìn tài liệu

- [ ] `FixedVector` · `FixedString` · `Span`
- [ ] CAN frame parser · DBC signal decoder · CAN arbitration simulator
- [ ] SOME/IP header serialize/deserialize · Service Discovery
- [ ] UDS session + DTC handler
- [ ] Unix domain socket IPC · shared-memory ring · epoll reactor
- [ ] Vẽ được stack CDC và bảo vệ được từng lớp
