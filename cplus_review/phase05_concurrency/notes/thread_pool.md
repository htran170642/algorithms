# W36 — `ThreadPool` (C++20 `jthread` + C++23 `move_only_function`)

> Artifact: [`include/thread_pool.hpp`](../include/thread_pool.hpp) (Lối A) ·
> [`include/thread_pool_mo.hpp`](../include/thread_pool_mo.hpp) (Lối B) ·
> [`tests/thread_pool_test.cpp`](../tests/thread_pool_test.cpp)
> Mục **6** để trống — tự viết. Đây là **component thật** (không throwaway): capstone của Phase 5, ghép
> `jthread` (W32) + hàng đợi cv (W33/34) + `packaged_task` (W35). Xây **2 lối** để thấy đúng chỗ khác nhau:
> queue chứa kiểu gì khi task vốn **move-only**.

---

## 0. Bằng chứng (full matrix xanh, 7 contract × 2 lối)

```
RunsTaskAndReturnsValue          : kết quả task về qua future.                              ✓
ForwardsArgumentsToTheCallable   : submit(f, a, b) forward args vào callable.               ✓
ExceptionPropagatesThroughFuture : throw trong task → get() ném lại (exception_ptr).        ✓
RunsEverySubmittedTaskExactlyOnce: 1000 task, Σ kết quả == N(N-1)/2 — không mất/lặp việc.   ✓
ActuallyRunsTasksInParallel      : n task ×40ms xong ~40ms (song song), không n×40ms.       ✓
DestructorDrainsQueuedTasks      : 200 task đã enqueue chạy HẾT trước khi dtor join.        ✓
WorkerCountIsAlwaysPositive      : hardware_concurrency()==0 vẫn ≥1 worker; sized(3)==3.     ✓

Chạy qua TYPED_TEST trên cả cr::ThreadPool (Lối A) và cr::ThreadPoolMO (Lối B):
  debug-20 · debug-23 · asan-23 · ubsan-23 · tsan-20 · tsan-23 → 14/14 PASS mỗi preset. TSan im lặng.
```

> `DestructorDrainsQueuedTasks` chính là test **bắt được SIGSEGV** member-order (mục 3) — code compile
> sạch, pass nhiều test khác, chỉ chết ở đúng ca drain-on-dtor.

---

## 1. Kiến trúc — pool = 3 viên gạch cũ ghép lại

```
submit(f, args…) ──packaged_task<R()>──▶ [ queue (mutex + cv) ] ──▶ jthread worker: task()
      │  get_future()                                                          │
      ▼                                                                        ▼
  std::future<R>  ◀───────────── kết quả / exception về qua shared state ──────┘
```

- **jthread (W32)** — N worker auto-join khi pool hủy. `n = max(1u, hardware_concurrency())`.
- **queue + cv (W33/34)** — `cv.wait(lk, pred)` chặn worker khi rỗng; `notify_one` khi có việc mới.
- **packaged_task (W35)** — mỗi callable nối sẵn một future ⇒ caller lấy được kết quả/exception về.

**Worker loop** (bất biến sống–chết của pool):
```cpp
cv_.wait(lk, [this]{ return stop_ || !tasks_.empty(); });  // pred PHẢI có stop_, không thì shutdown treo
if (stop_ && tasks_.empty()) return;                       // exit chỉ khi đã drain hết → graceful
task = std::move(tasks_.front()); tasks_.pop();
// … nhả khóa …
task();                                                     // chạy NGOÀI khóa, nếu không pool serialize trên mutex
```

**Graceful shutdown** (dtor): `{lock; stop_=true;}` rồi `cv_.notify_all()` (mọi worker phải thức để
thoát — khác `notify_one` ở submit). Worker thoát chỉ sau khi queue rỗng ⇒ việc đã enqueue vẫn chạy hết.
Submit sau khi `stop_` → **throw** (nếu enqueue thì future treo mãi, không worker nào chạy nữa).

---

## 2. ⭐ Hai lối — queue chứa kiểu gì khi task là MOVE-ONLY?

`packaged_task` **move-only** (sở hữu shared state duy nhất). Câu hỏi trung tâm: hàng đợi lưu nó thế nào?

**Lối A — `std::function<void()>` + `shared_ptr` (một type đồng nhất, chạy mọi chuẩn).**
`std::function` đòi target **copyable** → không chứa nổi `packaged_task` trần. Trick: bọc `packaged_task`
trong `make_shared` (copyable), enqueue một `function<void()>` chỉ copy shared_ptr rồi `(*task)()`.
```cpp
auto task = std::make_shared<std::packaged_task<R()>>( /* [f, …args] invoke */ );
std::future<R> result = task->get_future();
tasks_.emplace([task]{ (*task)(); });   // copy shared_ptr vào function
```
Giá: **1 control-block heap alloc thừa/task** (ngoài shared state của chính packaged_task).

**Lối B — phần tử queue MOVE-ONLY (bỏ hẳn shared_ptr).**
Nếu queue chấp nhận move-only, không cần trò copyable nữa. Capture `packaged_task` **by-move** vào lambda
`mutable`, đẩy thẳng lambda vào queue:
```cpp
std::packaged_task<R()> pt(/* … */);  std::future<R> result = pt.get_future();
tasks_.emplace([pt = std::move(pt)]() mutable { pt(); });   // callable move-only, KHÔNG shared_ptr
```
Queue element chọn theo chuẩn qua feature-test macro `__cpp_lib_move_only_function`:

| Type | Chuẩn | Shared state thừa? | Ghi chú |
|---|---|---|---|
| `std::move_only_function<void()>` | **C++23** | Không | Type-erase move-only thuần — đúng lỗ hổng C++23 sinh ra để lấp. |
| `std::packaged_task<void()>` | C++11 | **Có** (không dùng) | "Poor man's move_only_function": move-only nên chạy C++20, nhưng tự đẻ 1 shared state ta vứt đi. |

> `mutable` **bắt buộc**: `packaged_task::operator()` non-const; thiếu `mutable` thì `operator()` của
> lambda là const → không gọi được.

**So sánh:**

| | Lối A `ThreadPool` | Lối B `ThreadPoolMO` |
|---|---|---|
| Queue element | `std::function<void()>` (copyable) | move-only (`move_only_function` / `packaged_task<void()>`) |
| Lý do có mặt | `std::function` đòi copyable → cần `shared_ptr` | phần tử move-only → bỏ được `shared_ptr` |
| Alloc thừa/task | 1 (control block) | C++23: **0** · C++20 fallback: 1 (shared state phí) |
| Ưu điểm | đơn giản, 1 type, chạy pre-C++23 | ít alloc nhất khi có C++23 |
| Chọn khi | phải chạy C++20/cũ, ưu tiên đơn giản | đang C++23, tối ưu số alloc |

`std::function` vs `std::move_only_function` — **đây là lý do C++23 thêm cái sau**: để lưu callable
move-only (capture `packaged_task`, `unique_ptr`…) mà không phải bọc `shared_ptr`.

---

## 3. ⭐ Thứ tự HỦY member — bug SIGSEGV kinh điển (Bloomberg/Google)

Trong class **sở hữu thread**, member `jthread`/`thread` phải khai báo **CUỐI CÙNG**.

- Member hủy **ngược** thứ tự khai báo. Đặt `vector<jthread> workers_` **đầu** → nó hủy **cuối** →
  `m_`/`cv_`/`tasks_` hủy **trước** trong khi worker vẫn còn quay trên chúng (đặc biệt worker đang kẹt
  trong `cv_.wait` trên một `condition_variable` **đang bị hủy** = UB) → **use-after-destruction → segfault**.
- Đặt `workers_` **cuối** → hủy **đầu** → mỗi jthread join **khi** `m_`/`cv_`/`tasks_` **còn sống** → an toàn.

```cpp
std::queue<Task>          tasks_;
std::mutex                m_;
std::condition_variable   cv_;
bool                      stop_ = false;
std::vector<std::jthread> workers_;   // ⭐ LAST → destroyed FIRST → joins while state alive
```

Bài học: mọi RAII-class ôm thread — thread member đứng cuối. Compile sạch không cứu được; chỉ test
drain-on-dtor (và ASan/TSan) mới lộ ra.

---

## 4. Vì sao `submit` `notify_one` mà dtor `notify_all`

- `submit`: thêm **đúng 1** việc ⇒ đánh thức **1** worker là đủ; đánh thức cả bầy = thundering herd phí.
- dtor: `stop_=true` là điều kiện **mọi** worker phải thấy để thoát ⇒ **`notify_all`**. Nếu `notify_one`,
  chỉ 1 worker thức và thoát, N−1 worker còn lại ngủ mãi trong `cv_.wait` ⇒ jthread dtor **join treo** →
  deadlock lúc hủy.

---

## 5. Khi nào KHÔNG dùng thiết kế này

- **Task cần ưu tiên/hạn chót** → `std::queue` FIFO không đủ; cần `priority_queue` hoặc nhiều hàng đợi.
- **Work-stealing / scale tối đa** → một mutex + một queue là điểm nghẽn tranh chấp; cần per-worker deque
  + steal (kiểu TBB/Tokio). Pool này ưu tiên đơn giản & đúng, không phải throughput đỉnh.
- **Task rất ngắn, cực nhiều** → chi phí `packaged_task` shared state + (Lối A) control block lấn át việc;
  cân nhắc lô (batch) hoặc callable nhẹ không future.
- **Fire-and-forget không cần kết quả** → `packaged_task`/future là thừa; queue `move_only_function` trần
  (không future) là đủ.
- **Chỉ chạy 1 tác vụ nền** → `jthread` trần đủ rồi, không cần pool.
- **Không cần chạy pre-C++23** → ưu tiên Lối B (`move_only_function`), bỏ được alloc của Lối A.

---

## 6. TỰ VIẾT (retrieval — tự điền, không nhìn phía trên)

> Đề: **ThreadPool ghép từ 3 viên gạch nào (W mấy)? Worker loop: predicate của `cv.wait` PHẢI gồm gì và
> vì sao; vì sao chạy `task()` NGOÀI khóa? Graceful shutdown làm gì trong dtor, và vì sao `notify_all`
> chứ không `notify_one`? Lối A lưu task kiểu gì và vì sao phải `shared_ptr`; Lối B thay bằng gì và
> `std::function` vs `std::move_only_function` khác nhau chỗ nào? Vì sao member `jthread` phải khai báo
> cuối cùng — hỏng thì lỗi gì?** 8–10 câu.

_(câu trả lời của bạn ở đây)_

---

## 7. Câu hỏi phỏng vấn nối tiếp

- `submit` trả `future<R>` với `R = invoke_result_t<F, Args...>`. Nếu caller **vứt** future đi (không giữ),
  task vẫn chạy chứ? Và exception trong task sẽ đi đâu? (gợi ý: task vẫn chạy — pool giữ nó; exception nằm
  trong shared state, không ai `get()` thì **nuốt im lặng** khi shared state hủy — khác `async` là chỉ pool
  này thế.)
- Nếu một task **submit thêm task khác** vào chính pool, có deadlock không? Có, khi nào? (gợi ý: an toàn
  trừ khi task **chờ** future của task nó vừa submit **và** pool cạn worker rỗi → tất cả worker đều đang
  chờ nhau → deadlock. Bài toán "pool starvation".)
- `notify_one` ở submit có bao giờ **mất tín hiệu** (lost wakeup) không? (gợi ý: không — state đổi
  (`tasks_.emplace`) **dưới khóa** trước `notify`, predicate `!tasks_.empty()` bắt lại kể cả notify tới
  trước khi worker vào `wait`. Đây đúng bài học lost-wakeup W33.)
- Đo được gì để chứng minh Lối B rẻ hơn Lối A? (gợi ý: đếm alloc — override `operator new`/`operator delete`
  hoặc heaptrack; C++23 Lối B **thiếu đúng 1 alloc/task** so với A. Hoặc benchmark submit-rate task rỗng.)
- Muốn pool **chờ mọi task xong mà KHÔNG hủy pool** (kiểu `wait_idle()`), thêm gì? (gợi ý: đếm task
  in-flight + một `cv`; `wait_idle` chờ `in_flight==0 && tasks_.empty()`. Cẩn thận đếm giảm **sau** khi
  task chạy xong.)
- Vì sao chọn `jthread` chứ không `std::thread` cho workers? (gợi ý: `jthread` tự join ở dtor → RAII, không
  quên join → không `std::terminate`; `stop_token` ở đây không dùng vì ta tự quản shutdown qua `stop_`+cv,
  nhưng jthread vẫn cho auto-join miễn phí.)
- "What would a Staff Engineer improve?" — (gợi ý: `try_submit` non-throwing trả `expected`; giới hạn hàng
  đợi (bounded, back-pressure như W34) tránh OOM khi producer nhanh hơn worker; per-worker queue +
  work-stealing; `wait_idle()`; đặt tên thread để profiler; đo alloc & so 2 lối bằng benchmark; tùy chọn
  exception-handler thay vì nuốt im lặng.)
