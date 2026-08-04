# W34 — `latch` · `barrier` · `counting_semaphore` (C++20)

> Artifact: [`include/bounded_queue.hpp`](../include/bounded_queue.hpp) · [`tests/bounded_queue_test.cpp`](../tests/bounded_queue_test.cpp)
> Mục **6** để trống — tự viết. `BoundedQueue<T, Capacity>` là component **thật**: trả lời thẳng câu
> W33 để mở (queue không bound → RAM phình). Ba primitive còn được demo riêng trong test.

---

## 0. Bằng chứng (full matrix xanh, số đo thật)

```
BoundedFifoAndClose                    : đơn luồng, FIFO, close→push=false, close+empty→nullopt.   ✓
CloseWakesBlockedConsumer              : consumer kẹt trong pop() được close() đánh thức → nullopt.  ✓
BoundedConservesItemsAndRespectsCapacity: 4 prod × 50k = 200000 item, cap=64;
                                         count==200000; sum in==sum out; size KHÔNG BAO GIỜ > 64.  ✓
LatchStartGateAndDoneSignal            : 8 worker chờ start_gate; count_down 1→0 thả cùng lúc.     ✓
BarrierPhasedRoundsWithCompletion      : 4 thread × 5 vòng; completion fn chạy đúng 1 lần/vòng.     ✓
SemaphoreLimitsConcurrency             : 16 thread, 3 permit; max đồng thời ≤ 3.                    ✓

Ma trận: debug-20 · debug-23 · asan-23 · ubsan-23 · tsan-20 · tsan-23  → 6/6 PASS.
TSan im lặng qua stress semaphore + relay-wakeup (setarch -R để tắt ASLR).
```

> Bằng chứng cho **back-pressure**: cờ `capacity_violated` không bao giờ bật ⇒ dù producer nhanh hơn
> consumer, queue bị chặn ở `slots_free_.acquire()` nên không quá 64 item — RAM có trần. Đây chính là
> thứ W33 (unbounded) thiếu.

---

## 1. ⭐ Ba primitive — dùng đúng dụng cụ

`condition_variable` (W33) là dao đa năng nhưng dễ đứt tay (nhớ state-under-lock, `while` không `if`,
`notify_one`/`_all`...). Ba primitive C++20 dưới đây **gói sẵn một mẫu đồng bộ cụ thể** để bớt sai.

| Primitive | Mô hình | Tái dùng? | Dùng khi |
|---|---|---|---|
| `latch` | cổng đếm ngược **một chiều** về 0 | **không** (một lần) | "chờ N việc xong rồi đi tiếp": start-gate, fan-in join |
| `barrier` | rào **N thread gặp nhau** rồi thả cùng | **có** (tự reset) | tính toán **theo pha**: mô phỏng N vòng, lockstep |
| `counting_semaphore<N>` | đếm **permit** (giấy phép) | **có** | giới hạn tài nguyên/đồng thời; producer-consumer đếm slot |

Chi tiết cần khắc:

- **`latch`**: `count_down()` giảm (không chờ), `wait()` chờ về 0, `arrive_and_wait()` = gộp cả hai.
  Đếm **quá 0 là UB** — latch dùng một lần rồi vứt.
- **`barrier`**: `arrive_and_wait()` chờ đủ N. **Completion function** `barrier{N, fn}` chạy **đúng một
  lần, trên đúng một thread (không xác định thread nào)**, *sau* khi thread cuối tới và *trước* khi thả
  cả nhóm — chỗ lý tưởng để gộp kết quả pha mà **không cần khóa** (chỉ 1 thread chạy nó).
- **`counting_semaphore<N>`**: `acquire()` giảm permit, **block** nếu đang 0; `release()` tăng, đánh
  thức 1 waiter. `try_acquire()` / `try_acquire_for(dur)` = biến thể không/có-hạn-chờ.
  `binary_semaphore` = `counting_semaphore<1>`.

---

## 2. ⭐ Semaphore NHỚ tín hiệu — vì sao đếm producer/consumer dễ hơn cv

Khác biệt cốt lõi với `condition_variable`: **semaphore là một BỘ ĐẾM, nó nhớ tín hiệu**.
`release()` lúc chưa ai `acquire()` vẫn **cộng permit**; lần `acquire()` sau lấy được ngay.

Nhớ **lost wakeup** W33? cv **quên** `notify` bắn vào khoảng không ⇒ phải cẩn thận đổi-state-trong-khóa.
Semaphore **không quên** ⇒ không có khe lost-wakeup cho bài đếm tài nguyên. Đó là lý do
`BoundedQueue` đếm slot bằng hai semaphore thay vì hai cv:

```
slots_free_  khởi tạo Capacity   push: acquire() (chặn khi ĐẦY)   pop: release()
items_ready_ khởi tạo 0          push: release()                  pop: acquire() (chặn khi RỖNG)
```

Bất biến: `slots_free_ + items_ready_ == Capacity` (bỏ qua item đang bay giữa hai thao tác). Producer
"tiêu" 1 slot trống để tạo 1 item sẵn; consumer làm ngược lại. Không cần predicate, không `while`.

---

## 3. ⭐ Shutdown bằng RELAY — chỗ semaphore khó hơn cv

Đây là điểm **cv thắng semaphore**: `close()` của cv chỉ cần `notify_all()` đánh thức mọi waiter.
Semaphore không có "notify_all" — `release()` chỉ đánh thức **một**. Nếu 4 consumer đang kẹt ở
`items_ready_.acquire()`, một `release()` chỉ dậy được 1, ba đứa còn lại kẹt mãi.

**Vá — token relay (truyền gậy):**

```cpp
void close() { {lock; closed_=true;} items_ready_.release(); }   // thả 1 token

optional<T> pop() {
    items_ready_.acquire();
    lock;
    if (q_.empty()) {                 // dậy vì close, hết hàng
        unlock;
        items_ready_.release();       // RELAY: truyền token cho consumer kế
        return nullopt;
    }
    ... // còn hàng thì drain bình thường
}
```

Consumer đầu dậy → thấy rỗng+closed → **release lại** trước khi trả `nullopt` → consumer kế dậy → …
Một `release()` duy nhất lan hết hàng đợi. (Token chỉ luân chuyển khi `q_.empty()`, nên semaphore không
bao giờ vượt `Capacity`.) Đổi lại: shutdown phức tạp hơn cv một bậc — **trade-off có thật**.

> Kết luận thiết kế: bounded queue cần **cả** đếm slot (semaphore hợp) **lẫn** shutdown sạch (cv hợp).
> Bản này chọn semaphore + relay để học `counting_semaphore`; production nhiều nơi vẫn dùng
> **hai `condition_variable`** (`not_full` / `not_empty`) vì `notify_all` làm close() tầm thường.

---

## 4. Quyết định thiết kế & bẫy

- **Capacity là tham số template** (`std::ptrdiff_t`): `counting_semaphore<Capacity>` cần **max ở compile
  time** (chuẩn cho phép cài đặt chọn kiểu đếm theo max). Giá trị khởi tạo (`{Capacity}`, `{0}`) là
  runtime nhưng phải ≤ max.
- **`push` nhả slot lại khi đã đóng:** `acquire()` một slot rồi mới thấy `closed_` ⇒ phải `release()`
  trả slot, không thì rò permit.
- **`notify`/`release` NGOÀI khóa** (như W33): consumer dậy không kẹt lại ngay trên mutex.
- **Rule of Zero:** `mutex` + `semaphore` không copy/move ⇒ compiler tự xóa; `= delete` copy chỉ để nói ý.
- **`mutable mutex`** cho `closed()`/`size()` const (giống W32/W33).
- **Bẫy:** dùng 1 semaphore cho bounded queue (thiếu vế còn lại); quên relay khi close (consumer kẹt);
  `latch` đếm quá 0 (UB); tưởng completion fn của `barrier` chạy trên mọi thread (chỉ 1).

---

## 5. Khi nào KHÔNG dùng

- **`latch`** khi cần lặp lại nhiều vòng → dùng `barrier` (latch một lần là hết).
- **`barrier`** khi số thread thay đổi động giữa các pha → barrier gắn N cố định (có `arrive_and_drop`
  nhưng vụng); cân nhắc cv.
- **`counting_semaphore`** cho vùng loại trừ tương hỗ đơn giản → dùng `mutex` (semaphore không có khái
  niệm "chủ sở hữu", không chống double-release, không recursive).
- **BoundedQueue semaphore** khi cần shutdown/timeout/priority phức tạp → cv linh hoạt hơn.

---

## 6. TỰ VIẾT (retrieval — tự điền, không nhìn phía trên)

> Đề: **Phân biệt `latch` / `barrier` / `counting_semaphore` (mỗi cái một câu: mô hình + khi dùng).
> Vì sao semaphore đếm producer/consumer dễ hơn `condition_variable`? Trong `BoundedQueue`, hai
> semaphore đại diện cho gì và bất biến giữa chúng là gì? Vì sao shutdown cần token relay, và trong
> tình huống nào bạn sẽ chọn hai `condition_variable` thay cho hai semaphore?** 6–8 câu.

_(câu trả lời của bạn ở đây)_

---

## 7. Câu hỏi phỏng vấn nối tiếp

- Bất biến `slots_free_ + items_ready_ == Capacity` có đúng **mọi thời điểm** không? (gợi ý: không —
  giữa `acquire` và `release` trong một `push`/`pop` có "item đang bay", tổng tạm lệch 1; đúng ở trạng
  thái nghỉ. Đây là lý do không nên khẳng định bằng assert giữa chừng.)
- Nếu `close()` được gọi khi có producer **đang kẹt** ở `slots_free_.acquire()` (queue đầy) thì sao?
  (gợi ý: producer đó không được đánh thức — bản này giả định close() *sau* khi producer xong, như W33.
  Muốn wake producer thì close() phải `slots_free_.release()` thêm + push re-check closed_.)
- `barrier` completion function chạy trên thread nào? Có an toàn để I/O nặng trong đó không? (gợi ý:
  một thread bất kỳ trong nhóm, đúng 1 lần; các thread khác **đang chờ** nó xong ⇒ I/O nặng làm nghẽn
  cả pha — giữ nó ngắn.)
- Vì sao `counting_semaphore` cần max ở compile time mà `mutex` thì không? (gợi ý: cài đặt chọn kiểu
  bộ đếm/nền tảng theo max — vd `LeastMaxValue` nhỏ có thể map thẳng xuống futex/atomic hẹp.)
- Semaphore relay của bạn — có nguy cơ **thundering herd** không? (gợi ý: không, vì token truyền tay
  tuần tự từng consumer một, khác `notify_all` đánh thức đồng loạt.)
- Thay `BoundedQueue` semaphore bằng gì để có **timeout trên push** ("chờ tối đa 10ms rồi bỏ")? (gợi ý:
  `slots_free_.try_acquire_for(10ms)` trả `false` → drop; semaphore có sẵn API này, cv thì cần
  `wait_for` + predicate.)
