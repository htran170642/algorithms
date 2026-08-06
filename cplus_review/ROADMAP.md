# Senior C++ Mastery Roadmap — 59 Weeks

**Standard:** C++23 baseline + C++20 build matrix. Every artifact compiles and passes under **both**.
**Rule:** a week is not done until — artifact builds under both standards · tests pass under sanitizers · note written · Friday review survived.
**Don't reinvent the wheel:** where `std::` provides it, use it. Clones below are *throwaway learning exercises*, built once to see the machinery, then discarded.

## Progress

- [x] Week 0: Production Substrate
- [x] Phase 1: Modern C++ (W1–W10)
- [x] Phase 2: STL Deep Dive (W11–W20)
- [ ] Phase 3: Templates (W21–W26)
- [x] Phase 4: Memory Management (W27–W31)
- [ ] Phase 5: Concurrency (W32–W40)
- [ ] Phase 6: Performance (W41–W44)
- [ ] Phase 7: Linux & OS (W45–W49)
- [ ] Phase 8: Networking (W50–W53)
- [ ] Phase 9: Software Design (W54–W58)
- [ ] Week 59: Debugging & Production Hardening

---

# Week 0 — Production Substrate

*Phase 10 pulled forward. Tooling is the substrate, not the capstone: you cannot learn object lifetime without ASAN telling you when you're wrong, or `memory_order` without TSAN.*

- [x] Root `CMakeLists.txt`, `cmake/Warnings.cmake`, `cmake/Sanitizers.cmake`
- [x] `CMakePresets.json` — `{debug,asan,ubsan,tsan,release}` × `{-20,-23}`
- [x] `.clang-tidy` mechanizing the CLAUDE.md Coding Style
- [x] GoogleTest + GoogleMock + Google Benchmark via FetchContent
- [x] `check.sh` full-matrix gate
- [x] `scratch/scratch.cpp` — ASAN, LeakSanitizer, UBSAN each **observed firing** (heap-use-after-free, 4096-byte leak, signed overflow, oob shift)
- [x] `bench/bench_all.cpp` — the `DoNotOptimize` trap demonstrated: **0.000 ns vs 2433 ns**
- [x] TSan × kernel 6.x ASLR collision diagnosed → tests run under `setarch -R`
- [ ] Install `clang-tidy` + `valgrind` (`sudo apt install clang-tidy valgrind`)
- [ ] `git add` this folder — until then the clang-tidy gate silently passes on an empty file list
- [ ] Phase 6 debt: CPU frequency scaling is on; benchmarks are noisy until W41

---

# Phase 1 — Modern C++ (W1–W10)

> *"Master every topic before continuing."*

- [x] **W1 — Value categories.** Build `Probe` (logs every ctor/copy/move/dtor). Predict-then-verify: lvalue / xvalue / prvalue. *Reused all year.*
- [x] **W2 — Constructors & initialization.** default/copy/move/converting/`explicit`/delegating/inheriting · aggregate init · `initializer_list` traps · designated initializers
- [x] **W3 — Rule of 0/3/5 · RAII.** `ScopedFile`, `UniqueHandle` · `noexcept` · exception-safety guarantees (basic/strong/nothrow)
- [x] **W4 — Move semantics.** `std::move` · moved-from state · move-only types · when moves silently become copies. *Vehicle: build `String` with SSO.*
- [x] **W5 — Build `Vector`.** placement new · geometric growth · `emplace_back` · `move_if_noexcept`
- [x] **W6 — Perfect forwarding.** build `make_unique`, `invoke`, a forwarding factory · reference collapsing
- [x] **W7 — Virtual functions & destructors.** dynamic dispatch · virtual dtor · slicing · `override`/`final` · devirtualization · NVI
- [x] **W8 — Object model & layout.** vtable/vptr via `-fdump-lang-class` · `sizeof` of polymorphic types · multiple + virtual inheritance · ABI basics
- [x] **W9 — Object lifetime & UB.** dangling refs · temporary lifetime extension · `[[nodiscard]]` · UBSAN hunt
- [x] **W10 — `constexpr`/`consteval`/`constinit`**, concepts intro, modules *overview only*. → **Mock #1** + **Design round #1**

---

# Phase 2 — STL Deep Dive (W11–W20)

> *Understand implementation, complexity, iterator invalidation, trade-offs.*

- [x] **W11 — Iterators · `<algorithm>` · ranges.** iterator categories · write your own iterators · C++20 ranges/views/projections. *Enforces "STL algorithms over manual loops".*
- [x] **W12 — `vector` / `array` / `deque`.** Derive the **iterator-invalidation table from experiments**, not from memory.
- [x] **W13 — `list` / `forward_list`.** Benchmark vs `vector` → discover why `list` is almost never the answer.
- [x] **W14 — Build a HashMap** (open addressing + chaining) → `unordered_map`/`unordered_set`. Then explain *why* `std::unordered_map` is slow.
- [x] **W15 — Build an RB or AVL tree** → `map` / `set` / `multimap`. Ordered vs unordered.
- [x] **W16 — Build a binary heap** → `priority_queue`; `queue`/`stack` as adapters.
- [x] **W17 — `optional` · `variant` (+`visit`) · `any` · `std::expected`** *(C++23 — use it, don't clone it)*. Exceptions vs `expected` vs error codes.
- [x] **W18 — Build `StringView`**; `span` · dangling traps · `tuple`/`pair`
- [x] **W19 — Build `unique_ptr` / `shared_ptr` / `weak_ptr`.** control block · aliasing ctor · `enable_shared_from_this` · cycles
- [x] **W20 — `allocator` + `pmr`.** → **Mock #2** + **Design round #2**

---

# Phase 3 — Templates (W21–W26)

> *"Always explain why a feature exists."*

- [x] **W21** — function/class templates · deduction · CTAD · specialization vs overloading
- [x] **W22** — variadic templates + fold expressions: type-safe `printf`, a tuple-like
- [x] **W23 — Build your own `<type_traits>`** subset from scratch
- [x] **W24** — SFINAE / `enable_if` **then** Concepts — *why* concepts exist (error messages, overload-set control)
- [x] **W25 — CRTP** + static polymorphism. Benchmark against virtual dispatch.
- [ ] **W26** — consolidation → **Mock #3** + **Design round #3**

---

# Phase 4 — Memory Management (W27–W31)

- [x] **W27** — stack vs heap · memory layout · alignment · padding · `alignas`. Reorder structs to shrink them.
- [x] **W28** — placement new · manual lifetime · aligned storage · `std::launder` (overview)
- [~] **W29 — Arena/bump allocator** — concept note done (purpose, bump mechanic, no-op dealloc, pmr). Build deferred; `std::pmr::monotonic_buffer_resource` is the standard equivalent.
- [x] **W30 — Build a Pool allocator** (free list). Benchmark vs `new`/`delete`. (pool 2055 ns vs new/delete 13273 ns ≈ 6.5×)
- [x] **W31 — Cache locality** (AoS vs SoA) + **false sharing**, measured. (SoA 1994 µs vs AoS 9514 µs ≈ 4.8×; padded 36.8 ms vs packed 1017 ms ≈ 27×. `perf` cache-miss deferred: needs `sudo sysctl kernel.perf_event_paranoid=1`.) → **Design round #4** done; Mock #4 skipped (as W26).

---

# Phase 5 — Concurrency (W32–W40) — *hardest phase*

> Expect to be wrong often. That **is** the curriculum.

- [x] **W32** — `thread` + `jthread`/`stop_token` · `mutex` · `lock_guard`/`unique_lock`/`scoped_lock` · deadlock. **Wrote a data race; TSan caught it.** (RacyCounter: 197397/800000, ~75% lost updates + TSan two-stack report at `counter.hpp:25`; SafeCounter clean under tsan-20/-23. Racy demo quarantined behind `DISABLED_` so gate stays green. `setarch -R` auto-wrapped by root CMake.)
- [x] **W33** — `condition_variable` → **built `BlockingQueue<T>`**. Spurious + lost wakeups mastered. (state-change-under-lock defeats lost wakeup; `wait(lk,pred)` = the `while` loop defeats spurious; `push`→`notify_one`, `close`→`notify_all` avoids thundering herd; `close()` wakes blocked consumers → `nullopt` drain protocol. Conservation stress 4×4×50k=200k items, sum-in==sum-out, clean under tsan-20/-23. Real component — reused W36 ThreadPool, W56 producer-consumer.)
- [x] **W34** — `latch` · `barrier` · `counting_semaphore`. **Built `BoundedQueue<T, Capacity>`** (two `counting_semaphore`: `slots_free_` + `items_ready_`) — the back-pressure answer W33 left open. (semaphore REMEMBERS signals → no lost-wakeup for resource counting; invariant `slots_free_ + items_ready_ == Capacity`; shutdown via token **relay** since semaphores lack `notify_all`; trade-off noted — two `condition_variable` still preferred where clean shutdown dominates. `latch` = one-shot start-gate/fan-in; `barrier` = reusable phased rendezvous, completion fn runs once on one thread; semaphore standalone = concurrency limiter. Conservation stress 4×4×50k=200k, cap=64, `size` never exceeds Capacity, sum-in==sum-out; clean tsan-20/-23. 6/6 across debug-20/23, asan-23, ubsan-23, tsan-20/23.)
- [x] **W35** — `future` / `promise` / `packaged_task` / `async`. **Built a mini `Future<T>`/`Promise<T>`** (throwaway) over a `shared_ptr<SharedState>` = `mutex`+`cv`+`optional<T>`+`exception_ptr`+`ready` — the guts of `std::future`. Faithful contract: `get()` one-shot (moves the state out → invalid), `get_future()`/`set_*` twice throw `future_error`, and an exception set on the write end **rethrows out of `get()`** on the read end (`exception_ptr` crosses the thread boundary). Did NOT clone `packaged_task`/`async`/launch policies — those are properties of `async()`, demoed against real `std::`: `launch::deferred` is lazy & runs inline on the `get()` thread; `launch::async` guarantees a separate thread; the **un-stored-async-future dtor blocks** gotcha measured (4×20ms tasks run ~80ms = serial, not parallel). `packaged_task` = callable+shared-state, the W36 ThreadPool building block (move-only → needs `move_only_function`/type-erase in the queue). 8/8 across debug-20/23, asan-23, ubsan-23, tsan-20/23; TSan clean.
- [x] **W36 — Built a `ThreadPool`** *(concurrency capstone — `jthread` W32 + cv-queue W33/34 + `packaged_task` W35)*. Xây **2 lối** so sánh cách queue chứa task move-only: **Lối A `ThreadPool`** = `queue<function<void()>>`, gói `packaged_task` sau `shared_ptr` cho copyable (1 control-block alloc thừa/task, chạy mọi chuẩn); **Lối B `ThreadPoolMO`** = phần tử queue **move-only** — `move_only_function<void()>` (C++23, 0 alloc thừa) / `packaged_task<void()>` fallback (C++20), chọn qua `__cpp_lib_move_only_function` → capture `packaged_task` by-move vào lambda `mutable`, bỏ hẳn `shared_ptr`. `std::function` vs `std::move_only_function` = lý do C++23 sinh ra cái sau. Graceful shutdown: dtor `stop_=true`+`notify_all` (mọi worker phải thức; submit chỉ `notify_one`), worker drain hết queue rồi mới thoát; submit-on-stopped → throw. ⭐ **Bug SIGSEGV member-order**: `jthread` member phải khai báo **CUỐI** (hủy ngược thứ tự → worker join khi `m_`/`cv_`/`tasks_` còn sống); `DestructorDrainsQueuedTasks` bắt được. `worker()` chạy NGOÀI khóa tránh serialize. Test contract chạy `TYPED_TEST` trên **cả 2 lối** — 7×2=14/14 across debug-20/23, asan-23, ubsan-23, tsan-20/23; TSan im lặng.
- [ ] **W37 — Atomics + `memory_order`.** relaxed / acquire-release / seq_cst · litmus tests · atomic `wait`/`notify` · `atomic_ref`
- [ ] **W38 — Build an SPSC lock-free ring buffer**
- [ ] **W39 — Build an MPMC queue** (Michael–Scott or Vyukov) · ABA problem · hazard pointers (overview)
- [ ] **W40** — full TSAN sweep of everything built so far → **Mock #5** + **Design round #5**

---

# Phase 6 — Performance (W41–W44)

- [ ] **W41** — benchmarking discipline: Google Benchmark · `DoNotOptimize` · microbenchmark pitfalls
- [ ] **W42** — CPU cache · prefetch · branch prediction, measured with `perf stat`
- [ ] **W43 — Copy elision / RVO / NRVO** — verify with the `Probe` from W1. Find where moves *don't* happen.
- [ ] **W44** — SIMD awareness · autovectorization · optimization flags · reading asm → **Mock #6** + **Design round #6**

---

# Phase 7 — Linux & OS (W45–W49)

- [ ] **W45** — process / thread · `fork` / `exec` · scheduling · `/proc`
- [ ] **W46** — virtual memory · `mmap` · page faults · huge pages
- [ ] **W47** — signals · pipes · shared memory
- [ ] **W48** — `select` → `poll` → **`epoll`: build an echo server**
- [ ] **W49** — `strace` / `ltrace` → **Mock #7** + **Design round #7**

---

# Phase 8 — Networking (W50–W53)

- [ ] **W50** — TCP/IP · UDP · sockets · Nagle / `TCP_NODELAY` · backlog
- [ ] **W51 — Build an HTTP/1.1 server on epoll** (extends W48)
- [ ] **W52** — TLS / HTTPS overview · REST vs gRPC · Protocol Buffers · serialization
- [ ] **W53** — consolidation → **Mock #8** + **Design round #8**

---

# Phase 9 — Software Design (W54–W58) — **capstone**

- [ ] **W54** — SOLID + GoF patterns *in modern C++* — how templates and lambdas dissolve half of them
- [ ] **W55 — LRU Cache** + **Logger** (thread-safe)
- [ ] **W56 — Producer-Consumer** + **Connection Pool** (reuses the W30 pool allocator)
- [ ] **W57 — Plugin System** (`dlopen` · ABI stability · factory registration)
- [ ] **W58 — Reactor / Event Loop** ← **the whole point.** Composes epoll (P7) + ThreadPool (P5) + arena allocator (P4) + templates (P3) + RAII (P1).

---

# Week 59 — Debugging & Production Hardening

- [ ] **gdb** — breakpoints · watchpoints · core dumps · TUI · pretty-printers
- [ ] **Valgrind** — memcheck · callgrind · massif
- [ ] **GoogleMock** — `EXPECT_CALL` · DI for testability
- [ ] **perf + flamegraph** — full profile of the W58 Reactor
- [ ] Final full mock loop

---

# Cross-Cutting (every week, not separate weeks)

- **Coding Session Workflow** — clarify → brute force → better → optimal → complexity → edge cases → dry run → production impl → follow-up. *"Never skip reasoning."*
- **Challenges** — ~2/week in `challenges/`, per the LeetCode Rules. STL/idiom reps, not algorithm reps.
- **System Design** — one round per phase boundary (8 total) in `design/`, per the 11-step workflow.
- **Big-O** — discussed in every review. *"Always discuss."*

# Must Implement From Memory, By The End

- [ ] `String` (SSO) · `Vector` · `unique_ptr` · `shared_ptr` (control block)
- [ ] HashMap · RB/AVL tree · binary heap
- [ ] Arena allocator · Pool allocator
- [ ] Blocking queue · ThreadPool · SPSC ring buffer · MPMC queue
- [ ] LRU Cache · Logger · Connection Pool
- [ ] epoll echo server · HTTP/1.1 server · Reactor
