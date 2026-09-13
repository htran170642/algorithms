# Tuần 11 — Qt core: một thread, một loop, một queue

## 1. Ý chính

Tuần 10 bạn tự viết một middleware **không sở hữu thread nào**. `Runtime::poll()`
chạy callback trên thread của người gọi, nên một callback chậm làm đứng cả
process. Vì vậy `av_mw` đặt một luật cho mọi handler: **phải return**.

Qt là đúng hợp đồng đó, chỉ khác tên gọi. Học Qt event loop không phải học cái
mới — là biết tên chính thức của thứ bạn đã tự làm.

```text
av_mw                                Qt
while (!stop) runtime->poll()        QApplication::exec()
callback không được block             slot không được block
runtime->post(fn)                    Qt::QueuedConnection
weak_ptr trong mọi handler            QObject chết -> tự ngắt connection
runtime->fd()                        QSocketNotifier              <- tuần 12
```

Nên sản phẩm tuần này **không phải một UI**. Nó là **một cái thước và một thứ bị
hỏng để đo**: `av::qt::LoopMonitor`, và ba cái nút, mỗi nút làm đúng 3 giây việc.

## 2. Vì sao Cockpit DC cần chuyện này

- **Cluster là một deadline soft-real-time có kim đồng hồ gắn vào.** 60 Hz nghĩa
  là ngân sách 16 ms. Cái gì chiếm UI thread lâu hơn thì không phải "chậm", mà là
  **sai**: số trên màn hình là số cũ, và tài xế không có cách nào biết.
- **Gần như mọi việc trong cockpit đều blocking theo bản chất.** `read()` trên
  CAN socket, đọc file trên flash, gọi SOME/IP sang ECU khác. Event loop không
  phân biệt được `sleep` với một syscall blocking thật — nó chỉ biết là nó **không
  được trả lại quyền điều khiển**.
- **"UI bị treo" phải là một con số, không phải một cảm giác.** `worst 3012 ms`
  ghi được vào bug report; "hơi lag" thì không. Tuần 15 (fault injection) cần điều
  này.

## 3. Con số — phần quan trọng nhất của tuần

Cả bốn dòng dưới đây làm **đúng cùng một lượng việc**. Chỉ khác cách làm. Đây là
output thật của `apps/qt_loop`:

```text
                          ticks    p50    p99    worst
0. không bấm gì             305   15 ms  16 ms    17 ms
1. Block 3 s                125   15 ms  17 ms  3012 ms   <- gấp 177 lần ngân sách
2. Chunk 10 ms              311   15 ms  22 ms    34 ms
3. Worker thread            312   15 ms  16 ms    17 ms
```

Ba điều trong bảng này đáng giá hơn cả cái tiêu đề.

**`p50` bằng 15 ms ở cả bốn dòng.** Trung vị **không thấy** một cú đóng băng 3
giây. Trung bình cũng không: 3000 ms rải trên 300 mẫu chỉ là 10 ms trung bình.
Cột duy nhất phát hiện là `worst`. Đó là lý do màn hình in `worst`, và là lý do
ngân sách latency của cockpit luôn viết bằng **percentile, không bao giờ bằng
trung bình**.

**`ticks` tụt từ 305 xuống 125.** Một QTimer không kịp fire thì **không** fire bù
N lần sau đó — Qt **bỏ luôn** những lần bị miss. Nên loop bị block không chỉ
"đến muộn", nó **mất việc**. Và ai đếm số tick để đo thời gian sẽ đếm thiếu mà
không hay.

**Chunking có giá: 34 ms, không phải 17.** Trả quyền về loop mỗi 10 ms thì timer
16 ms vẫn có thể muộn một lát. Nó tốt hơn blocking 88 lần, và nó **không miễn
phí**. Nói thật thế này hữu ích hơn là đưa một con số tròn trịa.

## 4. Ba cách chữa, và khi nào dùng cái nào

```cpp
// 1. chính là cái bug
void Window::block_the_loop() { occupy(3000ms); }         // worst 3012 ms

// 2. chia nhỏ -- không thread, không mutex, không sinh failure mode mới
chunk_timer_.setInterval(0);                              // "vòng loop kế tiếp"
void Window::run_one_chunk() { occupy(10ms); if (--left_) return; finish(); }

// 3. đẩy đi -- việc thật sự blocking và không chia nhỏ được
worker_.moveToThread(&worker_thread_);
emit work_requested(3000);                                // tự động queued
```

**Cách 2 là câu trả lời đúng bất cứ khi nào việc có thể chia được**: không có
thread thứ hai nghĩa là không race, không phải lo cancel, không phải join.

**Cách 3 dành cho việc không chia được** — một `read()` blocking, một library
không chịu return. Và nó phải trả giá bằng ba nghĩa vụ mới: cancel, thứ tự
shutdown, và **tuyệt đối không chạm vào widget từ bên đó**.

## 5. Lifetime: `weak_ptr` của tuần 10, phiên bản Qt

```cpp
QObject::connect(&sender, &Sender::pinged, doomed.get(), &Receiver::on_pinged);
doomed.reset();
sender.fire(3);        // receiver đã chết thì bị bỏ; receiver còn sống vẫn nhận
```

Không ai gọi `disconnect()`. Destructor của `QObject` **tự huỷ đăng ký khỏi mọi
connection**. Đúng thứ mà tuần 10 phải dùng một cặp `shared_ptr`/`weak_ptr` để
làm bằng tay.

Cái bẫy là lambda, vì lambda không có destructor để Qt móc vào:

```cpp
connect(&sender, &Sender::pinged, context, [&ran] { ran = true; });  // an toàn
connect(&sender, &Sender::pinged,          [&ran] { ran = true; });  // vẫn compile
```

Overload thứ hai **có thật**, không bao giờ tự ngắt, và sẽ đọc bộ nhớ đã giải
phóng sau khi thứ nó capture chết. **Context object chính là lifetime** — bỏ nó
đi không phải là viết tắt.

## 6. Thread affinity là đường một chiều

```cpp
receiver->moveToThread(&worker);   // được: thread đang sở hữu nó "đẩy" nó đi
receiver->moveToThread(here);      // bị từ chối: "Cannot move to target thread"
```

Object **đẩy** sang thread khác được, nhưng **kéo về thì không**. Nên pattern là:
tạo → move một lần → để yên. Và muốn xoá thì nối `QThread::finished` với
`deleteLater`, chứ không delete xuyên qua biên. Điều này học được bằng cách viết
lời gọi đối xứng rồi đọc dòng Qt từ chối trên stderr.

## 7. Không có mutex nào cả

```cpp
connect(&worker_, &Worker::progressed, progress_, &QProgressBar::setValue);
worker_.moveToThread(&worker_thread_);          // connect trước, move sau
```

Connection **sống sót qua lần move**, và `Qt::AutoConnection` quyết định direct
hay queued **tại từng lần emit**, dựa trên affinity **lúc đó**. Sender ở worker
thread, receiver ở UI thread → Qt post một event. **Event được post đó chính là
cơ chế đồng bộ** — `BoundedQueue` tuần 2 và `post()` tuần 10, do framework cung
cấp sẵn.

Thứ duy nhất **không** dùng signal là cancel:

```cpp
void cancel() { cancelled_.store(true); }   // gọi được từ thread nào cũng được
```

Nếu cancel là một slot, nó sẽ bị xếp hàng **phía sau chính cái việc cần dừng**, và
chỉ chạy sau khi việc đó xong. Nên phải là atomic — lại là tuần 2.

## 8. Hai lỗi link và một bug thật, không cái nào đoán ra

**`undefined reference to LoopMonitor::staticMetaObject`.** AUTOMOC chỉ moc
những header **được liệt kê trong `SOURCES`** của target, hoặc nằm cùng thư mục
và cùng tên với một source. Header của mình ở `include/av/qt/` còn source ở
`src/`, nên moc không bao giờ thấy `Q_OBJECT`. Cách sửa là liệt kê header làm
source dù không có gì compile nó:

```cmake
add_library(av_qt src/loop_monitor.cpp include/av/qt/loop_monitor.hpp)
set_target_properties(av_qt PROPERTIES AUTOMOC ON)
```

**Kim đồng hồ lệch nửa pixel.** `painter.translate(width() / 2, height() / 2)` —
`translate` nhận `qreal`, nên phép chia nguyên **làm mất phần .5 trước khi** ép
sang double. Với width lẻ, kim quay quanh một tâm lệch nửa pixel và bị rung.
`bugprone-integer-division` bắt được, mắt thường thì không. Đúng loại bug mà
`-Wconversion` của tuần 1 nói đến.

## 9. clang-tidy gặp một framework già hơn guideline

Có 5 check bị tắt trong các thư mục Qt, và lý do **không giống nhau**.

| Check | Vì sao không làm theo được |
|---|---|
| `readability-identifier-naming` | `paintEvent`, `sizeHint` là camelBack vì base class như vậy; override mà đổi tên thì không còn là override |
| `cppcoreguidelines-owning-memory` | `new QPushButton(this)` **không** phải leak. QObject parenting **chính là** RAII — destructor của parent là chỗ giải phóng. Dùng `unique_ptr` sẽ thành **hai chủ sở hữu cho một object** |
| `readability-redundant-access-specifiers` | `signals:` và `public slots:` đều expand thành `public:`. Xoá `signals:` không phải dọn dẹp — nó làm moc **không compile được** |
| `misc-include-cleaner` | Nó đòi `qnamespace.h`, `qobjectdefs.h` — những header không chương trình Qt nào include. Làm theo nghĩa là viết `#include <QtCore/qobjectdefs.h>` chỉ để lấy một macro |

Cái thứ năm khác hẳn, và đáng nhớ:

> `misc-const-correctness` đòi `const Receiver receiver` và `const QApplication
> app`, vì code xung quanh không ghi vào chúng. **Nhưng Qt ghi** — qua
> connection, và qua con trỏ `qApp` mà constructor của nó cài. Khai báo một object
> `const` rồi sửa nó là **undefined behavior**. Ở đây linter không gợi ý dọn dẹp;
> nó gợi ý một cái bug.

Mọi finding **có thể sửa thì đã sửa trước**: 4 include thiếu trong
`loop_monitor.cpp`, 5 trong app, và phép chia nguyên ở trên. Các check vẫn bật ở
mọi nơi khác trong cây.

## 10. Chạy thử

```bash
./build/debug/apps/qt_loop/qt_loop
```

Bấm **1**, **2**, **3** lần lượt, đọc `worst` sau mỗi lần, bấm Reset giữa các lần.
Rồi làm cho đúng bài:

1. Bấm **1**, và **trong lúc cửa sổ đang đông cứng, bấm "Click me" 5 lần**. Bộ
   đếm không nhích — rồi nhảy luôn 5. Các click **chưa bao giờ bị mất**, chúng bị
   xếp hàng. *Đó chính là nghĩa của "UI bị treo".*
2. Nhìn cái kim thay vì nhìn số. Nó chỉ được điều khiển bởi một QTimer 16 ms,
   không gì khác; nó dừng vì **loop dừng**, và không có dòng code nào phải biết.
3. Bấm **2** rồi **3** và thử phân biệt hai cái **bằng mắt**. Bạn không làm được —
   và đó là lý do chọn cách 2 mỗi khi việc chia nhỏ được.

## 11. Bài tập gây lỗi

| Lỗi | Gây bằng cách | Bạn thấy gì | Con số |
|---|---|---|---|
| Slot bị block | nút 1 | kim dừng, click xếp hàng | `worst 3012 ms` |
| Block bị che | đọc `p50` sau nút 1 | không thấy gì cả | `p50 15 ms` |
| Mất tick | so `ticks` sau nút 1 và nút 0 | — | `125` vs `305` |
| Chunking không free | nút 2, đọc `p99` | — | `22 ms`, tăng từ `16` |
| Thiếu moc | bỏ header khỏi `SOURCES` | lỗi link | `undefined reference to staticMetaObject` |
| Lambda dangling | bỏ context object trong `connect` | chạy được, rồi đọc bộ nhớ đã free | ASan, preset `asan` |
| Move sai thread | thêm `moveToThread(back)` | `Cannot move to target thread` trên stderr | — |

Phá một luật rồi xem nó phản đòn:

1. Trong `Worker::run`, xoá đoạn kiểm tra `cancelled_`. Destructor của `Window`
   bây giờ ngồi trong `wait()` tới 3 giây — bấm nút 3 rồi đóng app, thấy chậm rõ.
2. Đổi `Qt::QueuedConnection` thành `Qt::DirectConnection` trong test
   `AQueuedConnectionWaitsForTheNextTripThroughTheLoop`. Nó fail ở
   `EXPECT_EQ(receiver.calls, 0)` — slot chạy ngay trong `fire()`, chính là bug
   `waiting = true` của tuần 10 trong bộ áo mới.

## 12. Câu hỏi phỏng vấn

1. Vì sao GUI không được block event loop? Trả lời bằng **millisecond**.
2. QTimer đặt 16 ms, một slot block 3 giây. Trong 3 giây đó timer fire bao nhiêu
   lần, và **sau đó** bao nhiêu lần?
3. Vì sao `p50` là thống kê sai cho UI latency, còn `worst` là đúng?
4. `Qt::AutoConnection` khi nào gọi trực tiếp, khi nào post? Quyết định **lúc
   nào**?
5. Ba cách chữa một việc dài trên UI thread. Cái nào không cần mutex, và vì sao
   đó là câu trả lời mặc định?
6. Qt chặn signal tới một receiver đã bị xoá bằng cách nào? Lambda phá vỡ điều
   gì, và cái gì khôi phục lại?
7. Có move một QObject **về** thread đã tạo nó được không? Vì sao không?
8. Vì sao `Worker::cancel()` là atomic chứ không phải một slot?
9. `staticMetaObject` từ đâu ra, và nêu **hai** cách làm nó biến mất.
10. Qt không được build với TSan. Làm sao vẫn giữ được một tuyên bố về thread cho
    trung thực?
11. Linter bảo bạn đặt `const` cho một object, và object đó là `QApplication`.
    Bạn làm gì, vì sao?
12. `av_mw::Runtime` không sở hữu thread và có `fd()`. Cái đó cắm vào event loop
    của Qt bằng cách nào?

## 13. Checklist

- [ ] Vẽ lại được bảng ánh xạ av_mw ↔ Qt từ đầu.
- [ ] Giải thích được vì sao `p50` bằng 15 ms ở cả bốn dòng.
- [ ] Giải thích được vì sao `ticks` tụt xuống 125 và không bao giờ được bù.
- [ ] Kể được ba cách chữa và nói được nên thử cái nào trước.
- [ ] Giải thích được context object làm gì trong một `connect` tới lambda.
- [ ] Giải thích được vì sao affinity là một chiều.
- [ ] Giải thích được vì sao `qt_loop` không có mutex nào, và vì sao `cancel()`
      là ngoại lệ.
- [ ] Giải thích được lỗi link `staticMetaObject`, cả hai nguyên nhân.
- [ ] Bảo vệ được cả 5 suppression của clang-tidy, và chỉ ra cái nào đang trỏ
      vào undefined behavior.
- [ ] Đã chạy demo, xếp 5 click vào một cửa sổ đông cứng, và đọc `worst` sau mỗi
      nút.

---

**Còn treo cuối tuần 11.**

- **`fd()` vẫn chưa được test.** `av_mw::Runtime::fd()` tồn tại để một event loop
  bên ngoài điều khiển được middleware, mà chưa có loop nào làm. Đó là việc đầu
  tiên của tuần 12, với `QSocketNotifier`.
- **Không có gì trong `av_qt` biết "xe" là gì** — có chủ ý. Đường nối giữa Qt và
  `av_can`/`av_mw` vẫn còn nhìn thấy được vì chưa có gì bước qua nó.
- **Cái kim không phải một cluster.** Nó không có dữ liệu phía sau — nó là đèn báo
  "còn sống" đội lốt đồng hồ. Tuần 13 sẽ cho nó thứ để chỉ vào.
- **Widgets, không phải QML.** Mọi thứ học được ở đây về loop, affinity và queued
  connection đều giữ nguyên giá trị; riêng code widget thì không.

**Tiếp theo:** tuần 12, C++ ↔ QML — `Q_PROPERTY`, một `VehicleModel`, và
`QSocketNotifier` cắm CAN fd vào event loop. Cái luật tuần này vừa đo sắp gặp dữ
liệu thật chảy vào từ một file descriptor.
