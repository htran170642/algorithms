# Tuần 5 — SocketCAN: bus trở thành một file descriptor

> Bản tiếng Việt của [w05_socketcan.md](w05_socketcan.md). Thuật ngữ kỹ thuật
> giữ nguyên tiếng Anh, vì đó là từ bạn sẽ gặp trong man page, trong kernel
> source, và trong phòng phỏng vấn.

## 1. Khái niệm

Điều đáng chú ý nhất ở SocketCAN là **nó mới ít đến mức nào**. Một CAN bus được
truy cập bằng `socket()`, `bind()`, `read()`, `write()` — đúng những lời gọi
tuần 3 đã dùng cho AF_UNIX, chỉ khác address family:

```c
socket(PF_CAN, SOCK_RAW, CAN_RAW)
```

Nên mọi thứ tuần 3 xây vẫn dùng được nguyên vẹn. `Poller` canh một CAN socket
**không sửa một dòng nào**, vì CAN socket *chính là* một descriptor bình thường.
Đó là cái hay của thiết kế: Linux đưa một fieldbus ra qua API mà mọi lập trình
viên đã biết, thay vì một bộ ioctl riêng của driver.

Ba thứ **thực sự** khác, và mỗi thứ đều từng làm ai đó mất cả buổi:

| | Là gì | Sai thì sao |
|---|---|---|
| **ifindex, không phải tên** | `bind()` nhận **số thứ tự** interface; phải dịch tên trước | không bind thẳng vào "vcan0" được; và vcan không bền vững — reboot là mất sạch |
| **Cờ nằm TRONG `can_id`** | 3 bit cao là `CAN_EFF_FLAG`, `CAN_RTR_FLAG`, `CAN_ERR_FLAG` | không mask → đọc ra `0x81ABCDEF` thay vì `0x1ABCDEF`, mọi tra cứu DBC đều trượt |
| **Kích thước là dấu hiệu phân biệt** | `read()` trả 16 byte (classic) hoặc 72 (FD) | **không có cờ nào để hỏi**; receiver đoán sai kích thước là parse sai toàn bộ |

Thêm hai sự thật chỉ quan trọng một lần, nhưng quan trọng rất nhiều:

- **Lọc diễn ra trong kernel.** `CAN_RAW_FILTER` quyết định frame nào mới được
  copy sang process. Trên một bus bận rộn, đây là khác biệt giữa "thức dậy vì
  mọi frame" và "thức dậy vì bốn frame mình cần".
- **Danh sách filter rỗng nghĩa là "không nhận gì cả"**, không phải "nhận tất
  cả". Một receiver xoá filter để "reset" sẽ **im lặng vĩnh viễn**.

## 2. Vì sao nó tồn tại trong Cockpit DC

Đây là tuần phần trừu tượng dừng lại. Tuần 1–4 **mô hình hoá** CAN; tuần 5 **nói
chuyện** với nó. Ba hệ quả cho một cockpit:

- **Đường nhận của cluster giờ là một fd thật**, nên nó ghép được với cùng cái
  event loop mọi thứ khác đang dùng — `epoll` ở đây, `QSocketNotifier` ở tuần
  12. Không cần một "CAN thread" riêng trừ khi có phép đo đòi hỏi.
- **`vcan` làm cả chuỗi test được mà không cần phần cứng.** Capstone phát triển
  được, test được, demo được trên laptop; lên target chỉ đổi tên interface.
- **Bộ đếm của tuần 4 trở nên quan sát được.** Bật `CAN_RAW_ERR_FILTER` thì
  kernel gửi TEC và REC như những lần đọc bình thường. Silent fault kết thúc
  tuần 4 — *node bus-off mà bus trông khoẻ hơn* — **không còn im lặng** với một
  chương trình biết hỏi.

## 3. Kiến trúc

```text
     sim_vehicle                              can_rx
   (mã hoá 7 signal)                  (giải mã, phát hiện stale)
          |                                     ^
       write()                                read()
          |                                     |
          v                                     |
   +---------------------------------------------------+
   |                  vcan0 (kernel)                    |
   |   acceptance filter . loopback . error frame       |
   +---------------------------------------------------+
                          |
                  (trên phần cứng thật)
                    CAN controller       <- tuần 4: arbitration, TEC/REC
                          |
                     CAN_H / CAN_L
```

`can_rx` lần đầu tiên nối tuần 3 và tuần 1 lại với nhau:

```text
Poller (tuần 3) -> CanSocket::receive (tuần 5) -> decode (tuần 1) -> giá trị vật lý
```

## 4. Cài đặt C++ / Linux

| File | Vai trò |
|---|---|
| [socket.hpp](../libs/av_can/include/av/can/socket.hpp) | `CanSocket`, `CanFilter`, `SocketStatus`, `FrameKind`, `BusErrorInfo` |
| [socket.cpp](../libs/av_can/src/socket.cpp) | socket `PF_CAN`, `if_nametoindex` + `bind`, filter, chế độ FD, error frame |
| [socket_test.cpp](../libs/av_can/tests/socket_test.cpp) | 13 case; 9 case cần `vcan0` tự skip khi nó vắng mặt |
| [common/vehicle_signals.hpp](../apps/common/vehicle_signals.hpp) | bảy signal của CLAUDE.md §7, trong hai message, dùng chung cho cả hai app |
| [sim_vehicle/main.cpp](../apps/sim_vehicle/main.cpp) | bộ phát 10 Hz |
| [can_rx/main.cpp](../apps/can_rx/main.cpp) | receiver dùng epoll, filter trong kernel, phát hiện stale, báo bus error |

Quyết định đáng bảo vệ:

- **`CanSocket` nằm trong `av_can`, không phải `av_ipc`.** Nó đặc thù CAN.
  Nhưng nó **tái sử dụng** `av_ipc::UniqueFd` thay vì mọc thêm một wrapper fd
  Rule-of-5 thứ hai — nên `av_can` giờ phụ thuộc `av_ipc`. Nhân bản ra sẽ dễ
  sửa hơn và là thiết kế tệ hơn.
- **Một bảng signal duy nhất, cả hai app cùng include.** Nếu là hai literal
  tách rời, sửa một bên sẽ sinh ra một con số sai nhưng hợp lý ở bên kia — đúng
  silent fault của tuần 1, tái xuất bằng copy-paste. Nó nằm ở `apps/` chứ không
  phải `libs/`, vì tuần 6 sẽ thay nó bằng file `.dbc` được parse, và lúc đó
  không phải xoá thứ gì khỏi library.
- **`WouldBlock` không phải lỗi**, và `EINTR` gộp chung vào đó. Cả hai đều nghĩa
  là "quay lại sau"; coi một trong hai là thất bại là cách một vòng poll biến
  thành busy-wait hoặc một cú shutdown vô cớ.
- **Receiver đọc cạn tới `EAGAIN` ở mỗi lần thức**, dù epoll level-triggered đã
  tha thứ cho việc đọc dở. Không phải vì tính đúng đắn — mà để một bên gửi bận
  rộn không làm chết đói phép kiểm tra stale ở vòng ngoài.
- **Stale là trạng thái do receiver tự tính**, không phải thứ bus báo về. Việc
  *không có gì tới* tự nó là một sự kiện, và đó là thứ duy nhất phân biệt một
  bộ phát đã chết với một bộ phát đang im.

## 5. Bài thực hành

`vcan` không bền vững, nên phải chạy lệnh này **một lần sau mỗi lần khởi động
máy**:

```bash
sudo ./scripts/setup_vcan.sh          # tạo vcan0
./check.sh                            # 10 test binary, 4 sanitizer, tidy
```

Rồi mở **ba terminal**:

```bash
./build/debug/apps/sim_vehicle/sim_vehicle vcan0     # 1: phát
./build/debug/apps/can_rx/can_rx vcan0               # 2: nhận và giải mã
candump -tz vcan0                                    # 3: công cụ chuẩn ngành
```

Terminal 3 mới là trọng tâm bài tập: `candump` và `can_rx` in ra **cùng những
byte đó**. Format output của `can_rx` được làm giống có chủ ý, để so từng dòng —
đó là cách bạn phân định *"bug nằm ở decoder của tôi hay ở trên bus?"* trong một
cái liếc mắt.

Bốn thí nghiệm:

1. **Tắt `sim_vehicle`.** Sau 500 ms `can_rx` báo `signals are stale`. Bật lại
   và xem `signals live again`. **Chính cái chuyển trạng thái đó**, không phải
   việc giải mã, mới là thứ một cluster cần.
2. **`cansend vcan0 100#DEADBEEF`** — frame 4 byte cho một message 8 byte.
   Decoder báo những signal không còn vừa là **unavailable**, không phải 0. Quy
   tắc tuần 1, vẫn còn hiệu lực.
3. **`cansend vcan0 300#0011223344556677`** — ID ngoài bộ filter. `can_rx`
   không bao giờ thấy; `candump` thì thấy. Kernel đã vứt nó trước khi copy.
4. **Bỏ `set_filters`** rồi chạy lại với `cangen vcan0 -g 1`. Giờ mọi frame trên
   bus đều đánh thức process.

## 6. Bài tập lỗi / debug

| Lỗi tiêm vào | Phát hiện bởi | Log ra | Fallback | Safe state |
|---|---|---|---|---|
| Interface không tồn tại (sau reboot) | `if_nametoindex` trả 0 | `ERROR no such CAN interface hint=sudo ./scripts/setup_vcan.sh` | từ chối khởi động | process thoát mã 1, không chạy mù |
| Bộ phát dừng | 500 ms không có frame | `WARN no frames -- signals are stale` | giữ giá trị cuối, đánh dấu invalid | stale nhìn thấy được từ UI |
| Frame ngắn hơn message | `decode` trả nullopt | `WARN signal unavailable` | bỏ signal đó, các signal khác vẫn giải mã | vắng mặt != bằng 0 |
| ID ngoài bộ filter | kernel, trước mọi lần copy | — | frame không bao giờ tới process | đúng ý đồ, không phải lỗi |
| Bộ filter rỗng | **không gì cả** — socket im lặng | không gì | không có | test `AnEmptyFilterSetDeliversNothing` khoá hành vi |
| Controller xuống cấp | `CAN_RAW_ERR_FILTER` | `ERROR bus error tec=.. rec=.. bus_off=..` | — | bộ đếm tuần 4, cuối cùng cũng đọc được |
| CAN-FD frame trên classic socket | **không gì cả** | không gì | không có | xem dưới |

### Silent fault của tuần 5

**Classic socket không bao giờ thấy FD frame, và không có gì báo điều đó.**

`CAN_RAW_FD_FRAMES` mặc định **tắt**. Nếu bên phát gửi CAN-FD mà bên nhận chưa
bật chế độ FD, kernel **vứt frame ngay ở đường vào**: không lỗi, không counter,
không dòng log, không `EAGAIN` nào mang ý nghĩa gì. Nhìn từ phía ứng dụng, ECU
đó chỉ đơn giản là đã ngừng nói.

`candump` **thấy** frame đang tới trên bus. Receiver **không thấy gì**. Khoảng
cách giữa hai quan sát đó *chính là* chẩn đoán — và nó chỉ hiện ra vì bạn đã
nghĩ đến việc chạy cả hai.

Khoá lại bởi test `CanSocket.AClassicSocketNeverSeesAnFdFrame`.

Bảng ghi chép đang chạy:

| Tuần | Silent fault | Vì sao không ai bắt được |
|---|---|---|
| 1 | sai byte order | ra số sai nhưng hợp lý; CRC và ACK đều pass |
| 2 | thiếu `close()` | không có output nào; `join()` treo mãi |
| 3 | thiếu `release` store | đúng trên x86, sai trên ARM |
| 4 | node rơi vào bus-off | bus trông **khoẻ hơn** trước |
| 5 | FD frame, classic socket | kernel vứt im lặng; `candump` thấy, bạn thì không |

## 7. Câu hỏi phỏng vấn cấp Senior

**1. `bind()` thực sự cần gì với một CAN socket, và vì sao nó hay hỏng thế?**

Một `struct sockaddr_can` mang `can_family = AF_CAN` và một `can_ifindex` dạng
số, lấy từ `if_nametoindex()` hoặc ioctl `SIOCGIFINDEX`. Nó hay hỏng vì `vcan`
không bền vững — mất sau mỗi lần reboot — và vì **index 0 là hợp lệ** và có
nghĩa là *mọi* CAN interface, nên một nhầm lẫn ở đó cho bạn một receiver âm thầm
nghe nhầm bus thay vì một thông báo lỗi.

**2. Vì sao phải mask identifier ở đường ra khỏi kernel?**

`can_id` **không phải** là một identifier; nó là identifier **cộng ba cờ** ở các
bit cao — `CAN_EFF_FLAG` (extended), `CAN_RTR_FLAG` (remote), `CAN_ERR_FLAG`
(error frame). Frame extended phải mask bằng `CAN_EFF_MASK`, frame standard bằng
`CAN_SFF_MASK`. Bỏ qua bước này thì mọi extended ID tới nơi đều có bit
`0x80000000` bật, nên mọi tra cứu DBC đều trượt và frame trông như *không biết
là gì* thay vì *sai*.

**3. Làm sao phân biệt classic frame với FD frame ở đường nhận?**

Bằng **số byte `read()` trả về**: 16 cho `struct can_frame`, 72 cho
`struct canfd_frame`. Không có cờ nào cả. Và FD frame chỉ tới được nếu socket đã
đặt `CAN_RAW_FD_FRAMES` — nếu không, kernel vứt chúng trong im lặng.

**4. Lọc CAN nên xảy ra ở đâu, và vì sao?**

Trong kernel, qua `CAN_RAW_FILTER`. Một bộ lọc đặt ở tầng ứng dụng vẫn tốn một
lần copy, một lần đánh thức và một lần context switch **cho mỗi frame**; trên
một bus 1000 frame/giây thì đó là phần lớn CPU của receiver dùng để vứt dữ liệu
đi. Cái bẫy là **danh sách filter rỗng nghĩa là "không nhận gì"**, nên một
receiver reset filter sẽ im lặng mà không báo lỗi.

**5. Một signal ngừng cập nhật trên cluster. Tuần 5 thay đổi bước đầu tiên của
bạn như thế nào?**

Chạy `candump` **song song** với ứng dụng. Nếu frame có trên bus mà ứng dụng
không có, lỗi nằm ở cấu hình socket — filter, chế độ FD, sai interface. Nếu
frame cũng không có trên bus, lỗi nằm ở hoặc dưới driver, và bộ đếm lỗi của tuần
4 là điểm dừng tiếp theo. Một phép so sánh duy nhất đó **cắt đôi không gian tìm
kiếm** và mất mười giây.

**6. Receiver phân biệt "giá trị bằng 0" với "không có giá trị" bằng cách nào?**

Nó **không thể**, nếu chỉ nhìn vào giá trị — và đó chính là lý do nó không được
phép thử. Receiver theo dõi thời gian kể từ frame cuối và báo cáo staleness như
một **trạng thái riêng**. Việc `decode()` trả `nullopt` cho một signal không vừa
frame, thay vì trả 0, là cùng một quy tắc ở tầng dưới. Một cockpit hiện 0 km/h
vì bus im lặng **tệ hơn** một cockpit không hiện gì.

**7. Vì sao chấp nhận được việc `av_can` phụ thuộc vào `av_ipc`?**

Vì `UniqueFd` đúng là thứ trừu tượng cần dùng, và nhân bản nó ra sẽ thành **hai
bản cài đặt Rule-of-5 phải giữ cho đúng**. Phụ thuộc chỉ chạy **một chiều**, từ
library CAN sang library IPC tổng quát — đó là chiều hợp lý: `av_ipc` không biết
gì về xe cộ.

## 8. Checklist tự kiểm

- [x] Giải thích được mà không cần nhìn note
- [x] Tự cài đặt, không phải chỉ đọc
- [ ] Cố ý làm hỏng và xem nó hỏng — **cần chạy `sudo ./scripts/setup_vcan.sh`
      trước**; bốn thí nghiệm ở §5 chính là bài drill
- [x] Bảo vệ được đánh đổi thiết kế đã chọn (§4)
- [x] Test pass dưới `./check.sh` — debug / asan / ubsan / tsan / tidy sạch,
      10 test binary

**Tình trạng thật:** 9 test cần `vcan0` hiện đang **SKIP**, vì tạo interface cần
quyền root và đó phải là thao tác của bạn, không phải của tôi. Chạy script setup
rồi `./check.sh` lại để chúng chuyển sang xanh. Tới lúc đó, tuần 5 mới chỉ được
kiểm chứng tới mức có thể kiểm chứng khi chưa có interface.

### Cố ý để lại cho sau

- **Đọc source `can-utils`** (`candump.c`, `cansend.c`) -> vẫn đáng làm; chúng
  ngắn, và `candump.c` là bản tham chiếu cho cái format mà `can_rx` bắt chước.
- **Bit timing** (`ip link set can0 type can bitrate 500000 sample-point 0.875`)
  -> chỉ có ý nghĩa trên phần cứng, mà `vcan` thì không có. Khái niệm nằm ở tuần
  4 §8.
- **`CAN_RAW_RECV_OWN_MSGS`** và `CAN_RAW_JOIN_FILTERS` -> chưa cần cho tới khi
  capstone có hai process trên cùng một interface.
- **BCM socket** (`CAN_BCM`, truyền tuần hoàn ngay trong kernel) -> đây mới là
  câu trả lời đúng cho một bộ phát định kỳ trong production; `sim_vehicle` dùng
  vòng lặp `sleep` vì chính vòng lặp `sleep` mới là thứ đang được dạy.
