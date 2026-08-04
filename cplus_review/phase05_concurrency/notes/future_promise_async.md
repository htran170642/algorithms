# W35 — `future` · `promise` · `packaged_task` · `async` (C++11, C++20 jthread)

> Artifact: [`include/future.hpp`](../include/future.hpp) · [`tests/future_test.cpp`](../tests/future_test.cpp)
> Mục **6** để trống — tự viết. `Future<T>`/`Promise<T>` mini là **clone học tập** (throwaway): dựng
> để thấy ruột `std::future` — dùng `std::future` mãi về sau. `packaged_task`/`async`/launch policy thì
> KHÔNG clone, chỉ demo `std::` thật vì chúng là tính chất của `async()`, không phải của `future`.

---

## 0. Bằng chứng (full matrix xanh, số đo thật)

```
PromiseDeliversValueAcrossThreads            : giá trị đi từ luồng producer → get() ở consumer.       ✓
ExceptionCrossesTheThreadBoundary            : set_exception ở luồng A → get() ném lại ở luồng B.      ✓
OneShotContract                              : get_future() lần 2 & set_value() lần 2 đều throw.       ✓
WaitThenGet                                  : wait() không tiêu thụ; get() sau đó vẫn lấy được.       ✓
PackagedTaskFeedsItsFuture                   : task chạy luồng khác, kết quả về qua future.            ✓
DeferredRunsLazilyOnTheCallingThread         : deferred KHÔNG chạy đến khi get(); chạy trên luồng gọi. ✓
AsyncRunsOnAnotherThread                     : launch::async ⇒ thread id khác caller.                 ✓
UnstoredAsyncFutureBlocksInDtorSoLoopIsSerial: 4 task×20ms KHÔNG giữ future ⇒ ~80ms (tuần tự!).        ✓

Ma trận: debug-20 · debug-23 · asan-23 · ubsan-23 · tsan-20 · tsan-23  → 6/6 PASS. TSan im lặng.
```

> Số đo cái bẫy: 4 task mỗi cái 20ms mà chạy **80ms** (=4×20). Nếu song song thật thì phải ~20ms.
> Chính dtor của future-do-`async`-đẻ chặn tại dấu `;` mỗi vòng ⇒ vòng lặp hóa **tuần tự**.

---

## 1. ⭐ future/promise = kênh MỘT LẦN, MỘT GIÁ TRỊ, giữa hai luồng

- `promise<T>` = **đầu GHI**, `future<T>` = **đầu ĐỌC**, nối nhau qua một **shared state** cấp trên heap.
- Producer: `p.set_value(x)` (hoặc `set_exception`). Consumer: `f.get()` **chặn** đến khi ready.
- **One-shot**: `p.get_future()` một lần; `f.get()` **tiêu thụ** state (`f.valid()` → false); `set_*` lần
  hai ném `future_error{promise_already_satisfied}`. Đọc nhiều lần → **`shared_future`** (copy được).

Ruột của bản mini (`future.hpp`) — đúng thứ `std::future` giấu bên trong:

```cpp
template <typename T> struct SharedState {
    std::mutex m; std::condition_variable cv;
    std::optional<T> value; std::exception_ptr error; bool ready = false;
};
```

`shared_ptr<SharedState>` ở cả hai đầu ⇒ đầu nào chết trước cũng không rớt state. `get()` = `cv.wait(ready)`
rồi trả `value` **hoặc** `rethrow_exception(error)`.

---

## 2. ⭐ Exception DU HÀNH qua luồng bằng `exception_ptr`

Đây là thứ cặp future/promise cho không mà thread trần không có: `p.set_exception(std::current_exception())`
ở luồng A ⇒ `f.get()` ở luồng B **ném lại đúng exception đó**. `exception_ptr` đóng gói exception, cho nó
"du hành" qua ranh giới luồng; `get()` gọi `rethrow_exception` để bung ra ở phía consumer. Không cần
mã lỗi thủ công, không mất kiểu exception.

---

## 3. ⭐ `async` launch policy — và cái bẫy dtor chặn

| Policy | Chạy khi nào | Chạy trên luồng nào |
|---|---|---|
| `launch::async` | **ngay**, đồng thời | luồng **khác** (bắt buộc) |
| `launch::deferred` | **lười** — chỉ khi `get()`/`wait()` | **chính luồng gọi get()** (inline) |
| mặc định `async(f)` = `async \| deferred` | impl tự chọn | không xác định |

- `wait_for` trên deferred **luôn** trả `future_status::deferred` (không bao giờ `ready` kiểu timeout).
- Muốn chắc chắn có luồng ⇒ truyền `std::launch::async` **tường minh**.

**⚠ Bẫy kinh điển:** future do `std::async` trả về, nếu **không giữ vào biến**, dtor của nó **chặn**
đến khi task xong. ⇒ `for (...) std::async(launch::async, task);` chạy **tuần tự** chứ không song song
(mỗi temporary chết tại dấu `;`, block ngay đó). Chỉ future từ `async` có tính chất này — future từ
`promise`/`packaged_task` dtor **không** block. Muốn fire-and-forget thật thì `async` là sai công cụ
(dùng ThreadPool ở W36, hoặc `jthread` detach có kiểm soát).

---

## 4. `packaged_task` — viên gạch của ThreadPool (W36)

`packaged_task<R(Args)>` = **gói callable + shared state** vào một object **di chuyển được (move-only)**.
`pt(args)` chạy hàm, đẩy kết quả (hoặc exception) vào `pt.get_future()`. Mẫu dùng chuẩn của thread pool:

```
caller:  auto f = pt.get_future(); queue.push(std::move(pt));   // move task vào hàng đợi
worker:  pt();                                                   // chạy, kết quả tự về future
caller:  f.get();                                                // lấy kết quả sau
```

Đây là lý do W35 đứng ngay trước W36: pool = `packaged_task` (đơn vị việc) + `BoundedQueue` (W34, hàng
đợi có back-pressure) + `jthread` (W32, worker).

---

## 5. Khi nào KHÔNG dùng

- **`async` để fire-and-forget** → dtor chặn làm nó tuần tự; dùng thread pool / `jthread`.
- **future/promise cho nhiều consumer hoặc nhiều lần đọc** → one-shot; dùng `shared_future`, hoặc cv/queue.
- **future/promise cho luồng dữ liệu liên tục** (stream) → chỉ một giá trị; dùng `BoundedQueue` (W34).
- **`packaged_task` khi không cần kết quả/exception trả về** → thừa shared state; `jthread` trần đủ.
- **tự viết `Future` như bản này trong production** → `std::future` đã có; bản mini chỉ để học ruột.

---

## 6. TỰ VIẾT (retrieval — tự điền, không nhìn phía trên)

> Đề: **future/promise là kênh gì (mấy lần, mấy giá trị, mấy luồng)? Shared state gồm những gì?
> Exception vượt luồng bằng cơ chế nào? Phân biệt `launch::async` vs `launch::deferred` (chạy khi nào,
> luồng nào). Cái bẫy dtor của future-từ-async là gì và hệ quả lên một vòng lặp `async`? `packaged_task`
> khác gì `promise`, và nó ghép vào ThreadPool ra sao?** 6–8 câu.

_(câu trả lời của bạn ở đây)_

---

## 7. Câu hỏi phỏng vấn nối tiếp

- `get()` của bạn `std::move(state_)` ra biến cục bộ trước khi `wait`. Vì sao vậy giúp future thành
  one-shot, và điều gì xảy ra nếu hai luồng cùng gọi `get()` trên **cùng** một future? (gợi ý: một đứa
  move được state, đứa kia thấy `state_==nullptr` → deref null; `std::future::get` cũng không an toàn
  gọi đồng thời — một future thuộc về một luồng.)
- Vì sao `set_value` phải `notify_all` chứ không `notify_one`? (gợi ý: hình dung nâng cấp lên
  `shared_future` — nhiều waiter; một tín hiệu phải đánh thức tất cả. Với single future thì one/all như nhau.)
- Nếu `promise` bị hủy mà **chưa** `set_*` gì thì `future.get()` nhận gì? (gợi ý: `std::` set
  `broken_promise`; bản mini hiện **treo mãi** — thiếu bước dtor của Promise set exception. Đó là một
  lỗ hổng để bạn tự vá: “Staff Engineer sẽ thêm gì?”)
- `launch::async` có tạo luồng **mới** mỗi lần không, hay có thể tái dùng pool? (gợi ý: chuẩn chỉ đảm
  bảo "as if" luồng riêng; GCC/libstdc++ tạo `std::thread` mới mỗi lần — không có pool, nên async
  không thay được thread pool về mặt chi phí.)
- Làm sao để `f.get()` có **timeout**? (gợi ý: `std::future` có `wait_for`/`wait_until` trả
  `future_status`; bản mini cần `cv.wait_for` + trả `optional`/status.)
- `packaged_task` là move-only. Vì sao không copy được, và điều đó buộc `ThreadPool` queue phải chứa
  kiểu gì? (gợi ý: nó **sở hữu** shared state duy nhất; queue phải move — `std::function` copyable
  không chứa nổi nó, cần `std::move_only_function` (C++23) hoặc type-erase thủ công.)
