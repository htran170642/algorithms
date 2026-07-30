# W33 — `condition_variable` & BlockingQueue (lost / spurious wakeup)

> Artifact: [`include/blocking_queue.hpp`](../include/blocking_queue.hpp) · [`tests/blocking_queue_test.cpp`](../tests/blocking_queue_test.cpp)
> Mục **6** để trống — tự viết. Component **thật** (không như counter W32): tái dùng ở W36 ThreadPool, W56 producer-consumer.

---

## 0. Bằng chứng (full matrix xanh, số đo thật)

```
FIFO + try_pop                     : đơn luồng, đúng thứ tự, empty→nullopt.           ✓
CloseWakesBlockedConsumer          : consumer kẹt trong pop() được close() đánh thức → nullopt (không treo). ✓
PushAfterCloseRejected             : push sau close() trả false, không enqueue.        ✓
ManyProducersManyConsumers (4×4)   : 4 prod × 50k = 200000 item; consumed_count==200000; sum in == sum out. ✓

Ma trận: debug-20 · debug-23 · asan-23 · ubsan-23 · tsan-20 · tsan-23  → 6/6 PASS.
TSan im lặng qua stress 200k item (setarch -R tự bọc bởi root CMake).
```

> Hai bằng chứng cho tính đúng đa luồng: (1) **conservation** — tổng giá trị đẩy vào == tổng lấy ra
> ⇒ không mất/không nhân bản item (nếu `push` không khóa → race trên `q_`, item lẫn/mất). (2) **không
> treo** — mọi consumer thoát vòng `while(pop())` khi `close()`; nếu `close()` dùng `notify_one` thay
> `notify_all`, hoặc `push` bắn notify vào khoảng không, test sẽ deadlock. TSan xanh = không thiếu
> happens-before edge nào.

---

## 1. ⭐ Lost wakeup — vì sao ĐỔI TRẠNG THÁI phải trong khóa

`condition_variable` **không nhớ** tín hiệu (khác semaphore). `notify` bắn lúc không ai đang chờ ⇒
bốc hơi. Race kinh điển nếu `push` đổi trạng thái *ngoài* khóa:

```
              queue rỗng
Consumer                         Producer
lock(m)
while(q.empty())  // TRUE
  ── chuẩn bị ngủ ──
                                 q.push(x)         // NGOÀI khóa
                                 cv.notify_one()   // bắn vào khoảng không
cv.wait(lk)       // ngủ SAU tiếng bắn → ngủ vĩnh viễn dù có hàng
```

Khe chết nằm giữa *kiểm predicate* và *thực sự block trong `wait`*. **Vá:** producer `push` **trong
khi giữ mutex**. `wait(lk)` nhả mutex **nguyên tử** với việc block, nên consumer giữ mutex suốt từ lúc
kiểm predicate tới lúc vào chờ ⇒ producer không lấy được khóa để push cho tới khi consumer đã an toàn
trong `wait` ⇒ không còn khe.

> Thần chú: **"Đổi trạng thái TRONG khóa; `notify` thì tùy (trong hay ngoài đều được)."**
> Cái cần bảo vệ là **trạng thái**, không phải lời `notify`.

---

## 2. ⭐ Spurious wakeup — vì sao `while`, không `if`

Thread có thể dậy mà **không ai** `notify` (spurious — cho phép bởi chuẩn, do cài đặt OS/futex). Nếu
dùng `if (empty) wait;` rồi cứ thế pop → pop trên queue rỗng = UB. Phải **kiểm lại điều kiện sau khi
dậy** ⇒ vòng lặp:

```cpp
while (!pred()) cv.wait(lk);        // dạng tường minh
cv.wait(lk, pred);                  // overload predicate = ĐÚNG vòng lặp trên, viết sẵn
```

Dùng overload `wait(lk, pred)` — vừa gọn vừa **không thể quên vòng lặp**. `pred` của ta:
`!q_.empty() || closed_` — xử lý cả spurious (dậy nhầm mà rỗng & chưa đóng → ngủ lại) lẫn shutdown
(§4). Lost (§1) + spurious (§2) đối nhau: một mất tín hiệu, một thừa tín hiệu — **cùng một khuôn**
`while(!pred) wait` + "đổi state trong khóa" trị cả hai.

---

## 3. ⭐ `notify_one` vs `notify_all` — thundering herd

| | Dùng khi | Vì sao |
|---|---|---|
| `notify_one` | một thay đổi phục vụ **một** waiter (push 1 item) | đánh thức đúng 1 consumer; không phí |
| `notify_all` | một thay đổi phục vụ **nhiều** waiter (đóng queue) | mọi consumer đều phải biết & thoát |

`push` → `notify_one` (1 item, 1 consumer lấy). `close` → `notify_all` (mọi consumer đang chờ đều phải
dậy để trả nullopt). Lạm dụng `notify_all` cho mỗi push = **thundering herd**: N consumer cùng dậy,
tranh 1 mutex, N-1 kiểm predicate xong ngủ lại — CPU cháy vô ích.

`notify` **ngoài** khóa (như artifact) tốt hơn: consumer dậy không bị chặn ngay bởi mutex producer
đang cầm — tránh "hurry up and wait". (Bên trong khóa vẫn đúng, chỉ kém tối ưu một nhịp.)

---

## 4. ⭐ Shutdown protocol — đánh thức & drain

`optional<T> pop()` trả `nullopt` = "hết hàng vĩnh viễn". Cơ chế:

1. `close()`: đặt `closed_ = true` **trong khóa**, rồi `notify_all()`.
2. predicate: `!q_.empty() || closed_` — consumer đang chờ dậy vì `closed_` thành true.
3. sau khi dậy: `if (q_.empty()) return nullopt;` — rỗng & đã đóng ⇒ báo hết. Nếu **còn** hàng thì cứ
   lấy tiếp (drain sạch rồi mới nullopt) — không mất item đã đẩy trước lúc đóng.
4. consumer: `while (auto v = q.pop()) { ... }` — `nullopt` là điều kiện thoát tự nhiên.

`push` sau `close()` trả `false` (không enqueue). Chọn `bool` thay vì ném exception: "đã đóng" là
kết cục **mong đợi** khi shutdown, không phải ngoại lệ.

---

## 5. Quyết định thiết kế & bẫy

- **Rule of Zero, nhưng có lý do:** `std::mutex` + `std::condition_variable` **không copy/move được**
  ⇒ compiler tự xóa copy/move của cả class. Ta `= delete` copy tường minh chỉ để nói rõ ý định; move
  vốn đã biến mất. Queue có waiter đang chờ thì copy/move vô nghĩa — chia sẻ qua ref/`shared_ptr`.
- **`mutable std::mutex`:** `closed()`/`size()` là `const` nhưng vẫn phải khóa (đồng bộ đối xứng, W32 §2).
- **`unique_lock` bắt buộc cho `wait`** (W32 §4): `wait` phải unlock→block→relock giữa scope;
  `lock_guard` không unlock được. Các hàm không `wait` (`push`/`try_pop`/`close`) dùng `lock_guard` —
  nhẹ hơn, đúng ý "1 mutex, suốt scope".
- **Bẫy thường gặp:** notify **trước** khi đổi state; quên `while` (spurious → pop queue rỗng);
  `notify_one` khi đóng (consumer khác kẹt mãi); đọc `size()` không khóa "để log".

---

## 6. TỰ VIẾT (retrieval — tự điền, không nhìn phía trên)

> Đề: **Giải thích lost wakeup xảy ra thế nào và vì sao "push trong khóa" vá được nó; spurious wakeup
> khác gì và vì sao phải `while` chứ không `if`; khi nào `notify_one` vs `notify_all`; và `pop()` trả
> `nullopt` theo protocol shutdown nào.** 6–8 câu.

_(câu trả lời của bạn ở đây)_

---

## 7. Câu hỏi phỏng vấn nối tiếp

- Queue này **không có bound** — producer nhanh hơn consumer thì sao? (gợi ý: RAM phình vô hạn; cần
  **bounded** queue + `cv_not_full` thứ hai để `push` cũng biết chờ — back-pressure. Đây là bài W34/W38.)
- Vì sao `notify` ngoài khóa lại tốt hơn trong khóa, mà đổi state thì bắt buộc trong khóa? (gợi ý:
  state race ⇒ lost wakeup; còn notify chỉ là đánh thức — thả khóa trước để consumer dậy không kẹt lại)
- Có thể thay cả `mutex + cv` bằng gì trong C++20 cho hàng đợi đơn giản? (gợi ý: `counting_semaphore`
  cho đếm slot — W34; hoặc atomic `wait/notify` — W37; lock-free SPSC ring — W38)
- `pop()` trả `optional<T>`. Nếu `T` là move-only (vd `unique_ptr`) có chạy không? (gợi ý: có —
  `std::move(q_.front())`; nhưng `push(T)` nhận by-value + move vào, cần T movable)
- TSan báo xanh — có chứng minh code không bao giờ deadlock/lost-wakeup không? (gợi ý: **không** —
  TSan là dynamic, chỉ thấy lịch chạy nó quan sát; nó bắt data race tốt nhưng không chứng minh vắng
  deadlock/lost-wakeup trên mọi interleaving. Cần model checker như CDSChecker/loom cho điều đó.)
- Nếu một consumer lambda **ném exception** giữa lúc giữ item vừa pop thì item đó mất — sửa sao cho
  exception-safe? (gợi ý: pop rồi mới xử lý ngoài khóa; hoặc RAII bọc item; đừng giữ mutex khi gọi
  callback người dùng — "không khóa khi gọi code lạ")
