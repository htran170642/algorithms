# Tuần 2 — Mô hình thread của cockpit

> Bản tiếng Việt của [w02_threads.md](w02_threads.md). Thuật ngữ kỹ thuật giữ
> nguyên tiếng Anh, vì đó là từ bạn sẽ gặp trong code, trong tài liệu, và trong
> phòng phỏng vấn.

## 1. Khái niệm

Ba thread, hai queue, và một quy tắc cho mỗi ranh giới:

```text
  [rx]  --frames-->  [decode]  --state-->  [ui]
   |                    |                     |
không bao giờ      chặn phía trên       có thể chậm
  bị chặn          không chặn phía dưới
```

Một bounded queue trước hết **không phải** là một container. Nó là một **hợp
đồng về việc điều gì xảy ra khi consumer không theo kịp**, và chỉ có đúng ba
câu trả lời:

| Chính sách | Khi nào đúng | Cái giá |
|---|---|---|
| chặn producer | producer đủ sức chờ; mất dữ liệu tệ hơn trễ | back-pressure lan ngược lên trên, có thể tới chỗ không chờ được |
| bỏ cái mới nhất | luồng event mà thứ tự quan trọng và event cũ vẫn còn giá trị | thông tin mới nhất lại là thứ bị mất |
| bỏ cái cũ nhất | state kiểu latest-value (tốc độ, RPM, nhiệt độ) | mất lịch sử; không sao, chẳng ai cần |

`BoundedQueue` cố tình **không cài** cái nào cả. Nó cung cấp `push` (chặn) và
`try_push` (báo `Full`), và mỗi caller tự chọn. **Một queue tự ý drop là một
queue nói dối producer của nó.**

Ý tưởng thứ hai là **close-then-drain**:

```cpp
while (const auto item = queue.pop()) { /* ... */ }   // tự kết thúc
```

`close()` chặn push mới và đánh thức mọi waiter, nhưng `pop()` vẫn tiếp tục trả
về các item còn trong hàng cho tới khi hết sạch. Chỉ khi đó nó mới trả
`nullopt`. Một quy tắc duy nhất đó xóa bỏ được cờ stop, timeout polling, và câu
hỏi "lúc shutdown có mất ba frame cuối không".

## 2. Vì sao nó tồn tại trong Cockpit DC

Cluster vẽ ở 60 Hz. CAN gửi tới 1 kHz. Hai tốc độ đó **sẽ không bao giờ khớp**,
và độ lệch phải được hấp thụ ở một chỗ **nói rõ ra**.

- Nếu receive thread bị chặn, kernel CAN socket buffer phía sau nó đầy lên và
  driver drop frame — vẫn mất từng ấy dữ liệu, nhưng giờ **vô hình và không quy
  trách nhiệm được**.
- Nếu queue không giới hạn, một cú kẹt UI biến thành memory leak, rồi thành OOM
  kill cả process cluster. Trên ECU thật không có swap để che.
- Nếu shutdown không graceful, tắt máy để lại frame xử lý dở và một process
  phải SIGKILL. Đó cũng là cách bạn mất các diagnostic trouble code đã xếp hàng
  mà chưa kịp ghi.

Demo biến đánh đổi đó thành số đo được chứ không phải lý thuyết: 200 frame vào,
25 hiển thị, 175 bị ghi đè **có khai báo**, **0 mất im lặng**.

## 3. Kiến trúc

```text
                        [ Cluster UI ]              tuần 13
                              |
                        [ Qt model ]                tuần 12
                              |
                        [ Middleware ]              tuần 8-10
                              |
    +-------------------------+-------------------------+
    |            BoundedQueue<VehicleState>             |  <- Ở ĐÂY
    |            capacity 4, drop-oldest                |
    +-------------------------+-------------------------+
                              |
                    [ decode thread ]                     <- Ở ĐÂY
                              |
    +-------------------------+-------------------------+
    |            BoundedQueue<CanFrame>                 |  <- Ở ĐÂY
    |            capacity 32, drop-on-full              |
    +-------------------------+-------------------------+
                              |
                      [ rx thread ]                       <- Ở ĐÂY
                              |
                  [ SocketCAN / vcan0 ]               tuần 5
```

`av_conc` và `av_ipc` (tuần 3) cố ý là hai library tách rời: một cái vượt ranh
giới **thread**, cái kia vượt ranh giới **address space**. Chế độ hỏng của
chúng khác nhau và code không nên giả vờ ngược lại.

## 4. Cài đặt C++ / Linux

| File | Vai trò |
|---|---|
| [libs/av_conc/include/av/conc/bounded_queue.hpp](../libs/av_conc/include/av/conc/bounded_queue.hpp) | toàn bộ library — template header-only |
| [libs/av_conc/tests/bounded_queue_test.cpp](../libs/av_conc/tests/bounded_queue_test.cpp) | 16 test, gồm 3 test concurrency tồn tại là để cho TSan |
| [apps/cockpit/main.cpp](../apps/cockpit/main.cpp) | ba thread, hai queue, shutdown có thứ tự, kế toán số liệu |

### Quyết định thiết kế đáng bảo vệ

**Ring buffer thay vì `std::queue`.** `std::queue` cấp phát ở mọi lần push.
Tuần 1 đã chọn `std::array` thay vì `std::vector` bên trong `CanFrame` đúng vì
lý do này; dùng `std::deque` ở đây sẽ vứt bỏ điều đó ở tầng trên. Cái giá là
một `static_assert` rằng `T` phải default-constructible, vì các slot được dựng
một lần lúc khởi động.

**Hai condition variable, không phải một.** Consumer vừa `pop` xong phải đánh
thức một *producer* đang chờ. Với một CV duy nhất bạn đánh thức tất cả và phần
lớn ngủ lại ngay — thundering herd, hiện ra thành CPU time khi tải cao.

**`notify_one` bình thường, `notify_all` khi close.** Close thay đổi một sự
thật mà **mọi** waiter phải quan sát được; báo cho một người sẽ để phần còn lại
ngủ mãi mãi.

**Notify ở ngoài lock.** Đánh thức một thread rồi để nó chặn ngay lập tức trên
cái mutex bạn vẫn đang giữ là context switch lãng phí.

**Dạng predicate của `wait`.** `wait(lock, pred)` kiểm tra lại sau mỗi lần thức.
Dạng trần `wait(lock)` là sai ở đây — spurious wakeup là có thật, và lost-wakeup
race (notify tới trước wait) cũng vậy.

**Constructor ném exception; data path thì không.** `capacity == 0` là lỗi lập
trình, bắt một lần lúc khởi động, nơi exception là tín hiệu ồn ào nhất và rẻ
nhất. Mọi thứ trên data path trả status thay thế. `main()` là một `try`/`catch`
bọc quanh `run()` để không gì thoát ra — exception rời khỏi `main` là
`std::terminate` không kèm thông báo.

**`size()` được ghi rõ trong doc là một lời nói dối.** Nó đúng tại thời điểm
lock được nhả và sai ngay sau đó. Dùng để log thì được; rẽ nhánh dựa trên nó là
một race ngay từ cấu trúc.

## 5. Bài thực hành

```bash
./check.sh              # debug + asan + ubsan + tsan + clang-tidy
./build/debug/apps/cockpit/cockpit
```

Hình dạng output mong đợi:

```text
  produced            200
  frames dropped      0     (rx queue full)
  decoded             200
  decode failures     0
  states overwritten  175   (ui queue full)
  displayed           25
```

Ba dòng cuối là bài học. UI chạy 10 ms/frame đối đầu producer 1 ms, nên khoảng
chín trên mười state bị thay thế trước khi kịp vẽ — và chương trình **nói ra
điều đó** thay vì giả vờ.

Thử nghiệm:

1. Nâng capacity của `states` từ 4 lên 200. Số ghi đè về 0, latency tới UI tăng
   lên hai giây. **Buffer to hơn không sửa được lệch tốc độ; nó chỉ đổi dữ liệu
   bị bỏ thành dữ liệu ôi.**
2. Đổi `try_push` phía dưới của decoder thành `push` chặn. Decoder thừa hưởng
   tốc độ của UI, frame queue dồn ứ, và rx bắt đầu drop. Back-pressure đã dịch
   chuyển tới đúng cái thread không hấp thụ nổi nó.
3. Xóa `frames.close()`. Decode thread chặn trong `pop()` mãi mãi và `join()`
   không bao giờ trả về — cú treo shutdown kinh điển, trong ba dòng.

## 6. Bài tập lỗi / debug

| Lỗi tiêm vào | Phát hiện bởi | Log ra | Fallback | Safe state |
|---|---|---|---|---|
| UI chậm hơn rx | `try_push` trả `Full` | đếm trong `states_overwritten` | bỏ cái cũ nhất, giữ cái mới nhất | luôn hiện giá trị mới nhất; xử lý staleness ở tuần 8 |
| rx queue đầy | `try_push` trả `Full` | đếm trong `frames_dropped` | bỏ frame | — |
| Frame bị cắt cụt | `decode` trả `nullopt` | `WARN decode incomplete frame` | bỏ qua frame, không sinh state | quy tắc tuần 1: vắng mặt != bằng 0 |
| `close()` trước khi consumer drain xong | — | — | không thể xảy ra: `pop` drain trước | shutdown không mất gì |
| `close()` không bao giờ được gọi | **không gì cả** — `join()` treo | không gì | không có | đây là lý do thứ tự shutdown là code, không phải quy ước |

Dòng cuối là bản đối ứng của tuần 2 với silent fault byte-order ở tuần 1: một
deadlock sinh ra **không** output nào cả. Không exception, không dòng log,
không crash dump — chỉ là một process không bao giờ thoát. Hai test
`Close.WakesABlockedPop` và `Close.WakesABlockedPush` tồn tại chính xác vì một
regression ở đó là vô hình cho tới lúc nó treo CI.

### Vì sao TSan quan trọng hơn việc test pass

`Concurrency.EveryItemArrivesExactlyOnce` chạy 4 producer và 3 consumer qua một
queue 16 slot. Nó **cũng pass với một cái lock hỏng**, phần lớn thời gian. TSan
là thứ biến "đã pass" thành bằng chứng: nó instrument mọi truy cập bộ nhớ và
báo cáo race bất kể lần chạy này kết quả có sai hay không.

## 7. Câu hỏi phỏng vấn cấp Senior

**1. Vì sao phải giới hạn queue? Bộ nhớ rẻ mà.**

Vì một queue không giới hạn biến lệch tốc độ thành bộ nhớ không giới hạn, và
chế độ hỏng chuyển từ "chúng ta drop frame" (nhìn thấy được, quy trách nhiệm
được, khôi phục được) thành "process cluster bị OOM-kill" (chí mạng, và xảy ra
40 giây sau nguyên nhân thật). Giới hạn cũng biến đánh đổi thành một quyết định
do người làm ra, thay vì để allocator quyết hộ.

**2. `push` chặn hay `try_push` — chọn thế nào?**

Bằng cách hỏi producer làm gì được với câu trả lời. Một CAN receive thread
không thể chờ có ích: kernel socket buffer phía sau nó tràn và drop đúng những
frame đó, chỉ là im lặng. Một thread replay từ file thì chờ được, và nên chờ.
Quy tắc: **back-pressure phải dừng lại ở component đầu tiên hấp thụ được nó, và
không bao giờ lan vào một component không hấp thụ được.**

**3. Vì sao `pop()` drain sau `close()` thay vì trả về ngay?**

Vì đóng nghĩa là "sẽ không có việc mới tới", không phải "vứt bỏ việc bạn đang
có". Trả về ngay sẽ làm mất bất cứ thứ gì đang xếp hàng tại thời điểm shutdown
— với một cockpit, có thể chính là các frame mang lỗi đã gây ra cú shutdown đó.
Drain cũng cho vòng lặp consumer một lối thoát tự nhiên, không cần cờ stop,
không cần timeout.

**4. Vì sao hai condition variable?**

Chúng biểu đạt hai predicate khác nhau: "not full" cho producer, "not empty" cho
consumer. Một CV duy nhất buộc phải đánh thức mọi waiter ở mọi thay đổi trạng
thái, và mỗi người lại đánh giá một predicate sai với phần lớn bọn họ.
Thundering herd đo được khi có tranh chấp.

**5. `size()` trả về 4 và capacity là 4. Kết luận được là queue đầy không?**

Không. Giá trị đó đúng khi lock còn được giữ và có thể đã sai rồi. Mọi
`if (queue.size() < queue.capacity()) queue.try_push(...)` đều là một
check-then-act race. Đó là lý do `try_push` làm phép kiểm tra **bên trong** cùng
một critical section với thao tác ghi, và lý do `size()` được ghi rõ là chỉ
dành cho logging.

**6. Priority inversion nằm ở đâu trong thiết kế này?**

Chưa có, vì mọi thread đều chạy ở priority mặc định. Nó thành thật ở tuần 12:
nếu UI thread chạy priority cao hơn decoder và cả hai tranh cùng một mutex, UI
có thể bị chặn bởi một thread priority thấp đang giữ mutex, trong khi một thread
priority trung bình preempt cái decoder đó. Cách sửa là priority inheritance
trên mutex (`PTHREAD_PRIO_INHERIT`) — đó là lý do QNX đặt nó làm mặc định còn
Linux thì không.

## 8. Checklist tự kiểm

- [x] Giải thích được mà không cần nhìn note
- [x] Tự cài đặt, không phải chỉ đọc
- [x] Cố ý làm hỏng và xem nó hỏng — ghi đè khi backpressure, frame cắt cụt, và
      cú treo shutdown do thiếu `close()`
- [x] Bảo vệ được đánh đổi thiết kế đã chọn — bounded thay vì unbounded, để
      caller chọn chính sách thay vì drop sẵn bên trong, close-then-drain thay
      vì một cái cờ
- [x] Test pass dưới `./check.sh` — debug / asan / ubsan / **tsan** / tidy sạch

### Cố ý để lại cho sau

- **Lock-free SPSC queue** -> sau tuần 15. Queue dùng mutex đủ nhanh để tới được
  capstone, và "chúng tôi thay nó vì profile bảo thế" là câu chuyện hay hơn
  "chúng tôi viết lock-free ngay từ đầu".
- **Timeout** (`push_for` / `pop_for`) -> tuần 8, nơi middleware cần một
  deadline chứ không phải một cái chờ vô hạn.
- **Staleness / last-known-good** -> tuần 8. Hiện tại một state bị bỏ là mất
  hẳn; vehicle data model mới là thứ biến điều đó thành "giá trị này cũ 3 giây".
- **Thread priority và CPU affinity** -> tuần 3, cùng phần còn lại của material
  về scheduling trên Linux.
