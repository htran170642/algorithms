# W32 — Thread, jthread, Mutex, Lock Guards, Deadlock & Data Race (TSan)

> Artifact: [`include/counter.hpp`](../include/counter.hpp) · [`tests/counter_test.cpp`](../tests/counter_test.cpp)
> Mục **7** để trống — tự viết. Mở **Phase 5** (concurrency — phase khó nhất, 9 tuần).

---

## 0. Bằng chứng (tsan-23 + debug-23, số đo thật)

```
SafeCounter (mutex)   : 8 thread × 100k inc  →  get() == 800000, TSan IM LẶNG. ✓
RacyCounter (no sync) : 8 thread × 100k inc  →  count = 197397 / 800000  (mất ~75% update)

TSan trên RacyCounter (chạy: setarch -R ...test --gtest_also_run_disabled_tests --gtest_filter='*RacyCounterRace*'):
  WARNING: ThreadSanitizer: data race
    Write of size 8 ... by thread T3:  cr::RacyCounter::inc()  counter.hpp:25
    Previous write of size 8 ... by thread T1:  cr::RacyCounter::inc()  counter.hpp:25
```

> Hai bằng chứng cho MỘT bệnh: (1) kết quả sai (197397 ≠ 800000 — lost update), (2) TSan chỉ
> thẳng hai stack cùng ghi một địa chỉ. Bằng chứng (2) mới là thứ tin được — (1) là UB nên
> "may thì thấy", còn TSan phát hiện *quan hệ happens-before bị thiếu*, không phụ thuộc vào việc
> race có tình cờ lộ ra lần chạy này hay không.

`setarch -R` (tắt ASLR) bắt buộc cho TSan trên kernel 6.x — shadow memory của TSan map ở địa chỉ
cố định, ASLR 32-bit thỉnh thoảng đè lên → `unexpected memory mapping` (đã chẩn ở Week 0). Root
CMake tự bọc `setarch -R` khi `CR_SANITIZER=thread`.

---

## 1. ⭐ Data race là khái niệm của MEMORY MODEL, không phải "chạy trùng giờ"

Định nghĩa chuẩn (`[intro.races]`): hai truy cập tới **cùng địa chỉ**, **ít nhất một là ghi**,
**không** có quan hệ *happens-before* giữa chúng, và **không** phải atomic ⇒ **UB**.

- **UB, không phải "sai số".** Compiler được phép giả định race không xảy ra → cache biến vào
  thanh ghi, reorder, xóa code. "197397" chỉ là *một* biểu hiện; lần khác có thể ra số khác,
  hoặc tearing, hoặc giá trị vô nghĩa. Vì thế **không đo bằng mắt được** — cần TSan.
- `++value` là **read-modify-write** (đọc → +1 → ghi). Hai thread xen kẽ giữa đọc và ghi ⇒
  cùng đọc `k`, cùng ghi `k+1` ⇒ mất một lần tăng. 75% mất update ở trên là chồng chất chuyện đó.

---

## 2. ⭐ Đồng bộ phải ĐỐI XỨNG (bẫy quiz Q5)

`SafeCounter::get() const` **vẫn khóa** `m_`. Vì sao đọc cũng phải khóa?

- Một bên ghi có khóa, một bên đọc *không* khóa ⇒ **không có happens-before** giữa chúng ⇒ vẫn
  data race UB. "Đọc int là atomic trên x86" là đúng ở tầng CPU nhưng **sai ở tầng C++**:
  compiler thấy biến thường nên được phép cache/reorder bất kể phần cứng.
- `get() const` khóa `m_` ⇒ `m_` phải `mutable` (const method vẫn được đổi trạng thái đồng bộ).
- Sửa đúng: cả hai bên **cùng một** cơ chế — cùng `m_`, HOẶC đổi sang `std::atomic<long>` (W37).

---

## 3. ⭐ `thread` vs `jthread` — RAII cho thread

| | `std::thread` (C++11) | `std::jthread` (C++20) |
|---|---|---|
| Dtor khi còn joinable | **`std::terminate()`** | tự `request_stop()` rồi `join()` |
| Xin dừng hợp tác | không có | `stop_token` (thread tự kiểm `stop_requested()`) |
| Dùng khi | cần `detach()` thật sự | **mặc định** |

Trong `hammer()`: `vector<jthread>` ra khỏi scope → mọi thread tự join, **kể cả khi `EXPECT_EQ`
throw trước đó**. Nếu là `std::thread` mà assertion ném giữa chừng → thread còn joinable lúc unwind
→ `std::terminate`. `jthread` = fix đúng chỗ.

> `stop_token` là **hợp tác**, không phải kill: không có cách cưỡng bức giết thread trong C++
> chuẩn (khác `pthread_cancel`). Thread phải tự nguyện kiểm tra và thoát.

---

## 4. ⭐ Bộ ba khóa — chọn cái nào

| RAII lock | Khi nào | Vì sao |
|---|---|---|
| `lock_guard` | 1 mutex, khóa suốt scope | tối giản, C++11, không overhead thừa |
| `scoped_lock` | **mặc định** — 1 *hoặc nhiều* mutex | khóa ≥2 mutex bằng thuật toán tránh deadlock (`std::lock`) |
| `unique_lock` | cần linh hoạt | `unlock()` giữa chừng · `defer_lock`/`try_lock` · move được · **bắt buộc cho `condition_variable`** (W33) |

`SafeCounter::inc()` dùng `lock_guard` (1 mutex, đủ). `scoped_lock` toả sáng khi khóa nhiều —
xem §5.

---

## 5. ⭐ Deadlock — 4 điều kiện Coffman & lock ordering

Kinh điển: T1 giữ `a` đợi `b`; T2 giữ `b` đợi `a` → **circular wait** → treo.

Bốn điều kiện (đủ cả 4 mới deadlock): mutual exclusion · hold-and-wait · no preemption ·
**circular wait**. Phá `circular wait` là dễ nhất:

- **Lock ordering toàn cục:** mọi thread luôn khóa theo cùng thứ tự (vd theo địa chỉ tăng dần).
  ```cpp
  void transfer(Account& from, Account& to, long amt) {
      std::scoped_lock lk(from.m, to.m);   // khóa CẢ HAI, thuật toán tránh deadlock
      from.bal -= amt; to.bal += amt;
  }
  ```
  `scoped_lock lk(a, b)` gọi `std::lock(a,b)` bên trong — try-and-backoff, không phụ thuộc thứ
  tự bạn truyền vào ⇒ `transfer(x,y)` và `transfer(y,x)` chạy song song **không** deadlock.
- `volatile` **KHÔNG** liên quan gì (bẫy quiz Q4) — nó không tạo happens-before, không đồng bộ.

---

## 6. Sai lầm phổ biến ngoài production

- Đọc biến chia sẻ "chỉ để log/metric" mà không khóa → race âm thầm (đối xứng, §2).
- `std::thread` quên `join()` trên đường lỗi (exception) → `terminate`. Dùng `jthread`.
- Khóa hai mutex mỗi hàm một thứ tự khác nhau → deadlock ngẫu nhiên. Dùng `scoped_lock` đa mutex.
- Tin "x86 đọc/ghi aligned là atomic nên khỏi đồng bộ" → đúng phần cứng, **sai** memory model C++.
- Chạy TSan không tắt ASLR trên kernel 6.x → `unexpected memory mapping`, tưởng flaky (Week 0).

---

## 7. TỰ VIẾT (retrieval — tự điền, không nhìn phía trên)

> Đề: **Giải thích vì sao `++counter` từ 2 thread không đồng bộ là UB (không chỉ "sai số"), vì
> sao một bên đọc không khóa vẫn là race, `jthread` khác `thread` ở đâu, và `scoped_lock(a,b)`
> tránh deadlock kiểu gì.** 5–6 câu.

_(câu trả lời của bạn ở đây)_

---

## 8. Câu hỏi phỏng vấn nối tiếp

- TSan báo "no race" — có nghĩa code **chắc chắn** không race không? (gợi ý: TSan chỉ thấy đường
  chạy nó *quan sát được*; race trên nhánh chưa chạy vẫn lọt — TSan là dynamic, không phải chứng minh)
- `SafeCounter` đúng nhưng chậm (mutex mỗi inc). Thay bằng gì nhanh hơn cho một bộ đếm? (gợi ý:
  `std::atomic<long>` + `fetch_add(memory_order_relaxed)` — W37)
- `stop_token` là hợp tác. Làm sao dừng một thread đang kẹt trong `read()` blocking? (gợi ý: không
  dừng được bằng token; cần đánh thức qua fd/eventfd, self-pipe, hoặc `pthread_kill` — Phase 7)
- Vì sao `scoped_lock` không cần bạn truyền mutex đúng thứ tự, mà `lock_guard` hai cái thì có?
  (gợi ý: `std::lock` dùng try-lock-all + backoff, không khóa tuần tự)
- `mutable std::mutex` trong type "logically const" — có phá const-correctness không? (gợi ý:
  không — const nghĩa là *quan sát* không đổi; đồng bộ nội bộ là chi tiết cài đặt, xem `atomic` cũng vậy)
- Nếu `hammer` dùng `std::thread` và một lambda ném exception thì sao? (gợi ý: `terminate` — không
  bắt được qua `join`; đây là lý do `jthread` + tại sao exception trong thread phải bắt tại chỗ)
