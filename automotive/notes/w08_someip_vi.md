# Tuần 8 — SOME/IP: từ phát thanh tín hiệu sang gọi dịch vụ

> Bản tiếng Việt của [`w08_someip.md`](w08_someip.md). Thuật ngữ tiếng Anh giữ
> nguyên, vì đó là chữ bạn sẽ gặp trong tài liệu chuẩn, trong code và trong
> phỏng vấn.

---

## 1. Khái niệm

### Ba giai đoạn

| Tuần | Giống cái gì | Làm được gì |
|---|---|---|
| 4–6 · CAN | **loa phát thanh** trong phòng | ai cũng nghe, không ai hỏi được |
| 7 · UDP multicast | **bưu điện** gửi thư hàng loạt | vẫn là phát thanh, chỉ nhanh hơn |
| **8 · SOME/IP** | **điện thoại** | **gọi cho ai đó → họ trả lời BẠN** |

Tuần 4 đến 7 chỉ làm được đúng một việc: **thông báo**. Tuần 7 đổi công nghệ
truyền dẫn nhưng **không đổi bản chất** — `eth_gw` phát ra, ai nghe thì nghe.
Không ai hỏi được gì.

```text
tuần 4-7    CAN ID  ──►  message  ──►  signal
tuần 8      service ──►  method / event / field
```

Một message SOME/IP gọi tên **dịch vụ**, nói làm gì với nó, và mang theo **ai
hỏi** + **lần thử thứ mấy**:

```text
┌─ 16 byte, TOÀN BỘ big-endian ──────────────────────────────────┐
│  Message ID   │ Service ID (2) │ Method ID (2)                 │
│  Length       │ 4 byte -- xem cạm bẫy bên dưới                 │
│  Request ID   │ Client ID (2)  │ Session ID (2)                │
│  Proto ver 1  │ Iface ver 1    │ Msg type 1 │ Return code 1    │
└────────────────────────────────────────────────────────────────┘
```

Hai cạm bẫy nằm trong cái hộp đó, và cả hai **vô hình** cho tới khi gặp stack
của hãng khác:

- **Length KHÔNG đo cả message.** Nó đếm **từ Request ID trở đi**: 8 byte cộng
  payload. Payload rỗng thì ghi 8, không phải 16 và không phải 0. Hai bản cài
  đặt cùng sai một kiểu sẽ chạy với nhau hoàn hảo — và với người khác thì
  không.
- **Toàn bộ big-endian**, va chạm trực diện với tuần 6: `@1` trong
  `cockpit.dbc` là Intel/little-endian. Hai quy ước ngược nhau, **trong cùng
  một tiến trình**, và không compiler nào cảnh báo.

Method id chia đôi bằng bit 15: `0x0000-0x7FFF` là method, `0x8000-0xFFFF` là
event. Không phải một trường riêng — chỉ là quy ước của chuẩn.

### Ba thứ này KHÔNG cùng một tầng

Đây là chỗ hay nhầm nhất, và cũng là một câu phỏng vấn trá hình:

```text
        SOME/IP          giao thức ỨNG DỤNG
           │
        UDP / TCP        tầng VẬN CHUYỂN
           │
          IP
           │
       Ethernet          DÂY
```

**SOME/IP chạy TRÊN UDP.** Chúng không thay thế nhau. `apps/svc_server` link
đồng thời cả `av_service` lẫn `av_eth`, vì cái này nằm trên cái kia.

CAN mới là lựa chọn thay thế thật, vì nó là **một sợi dây khác hẳn**.

Nên câu hỏi thật là **hai câu**, không phải ba lựa chọn:

| Câu hỏi | Trả lời |
|---|---|
| CAN hay Ethernet? | Nhỏ + chu kỳ + liên quan an toàn → **CAN**. Lớn hoặc theo yêu cầu → **Ethernet**. |
| Trên Ethernet dùng gì? | Cần ngữ nghĩa dịch vụ, nhiều team, cần discovery → **SOME/IP**. Luồng lớn một chiều, mình viết cả hai đầu → **UDP thô / RTP**. File phải toàn vẹn → **TCP**. |

Và bản thân SOME/IP lại chọn tiếp: **event đi UDP** (mất một mẫu thì mẫu sau đã
thay thế), **method payload lớn đi TCP** (không có gì thay thế được một byte
thiếu). Đúng lập luận head-of-line blocking của tuần 7.

---

## 2. Vì sao nó tồn tại trong Cockpit DC

### Bài toán khởi động

Cụm đồng hồ vừa bật lên và phải hiện mức xăng **ngay bây giờ**.

```text
CAN:       0.0s  cluster bật -- màn hình trống, không biết gì
                 ...chờ...
           1.0s  ECU nhiên liệu phát chu kỳ tiếp theo

SOME/IP:   0.0s  cluster bật
           0.0s  GetFuelLevel()  ──►
           0.0s  ◄── 42.5 %                (85 us sau, đo được)
```

Trên CAN **không có cách nào để hỏi**. Nên mọi cockpit đều cần cả hai dạng:
`GetSpeed()` cho lúc *ngay bây giờ* — khi khởi động, hoặc khi người dùng vừa mở
một màn hình — và `OnSpeedChanged` cho luồng liên tục lúc đang chạy.

### Thất bại giờ có tên

Gửi một ID không ai quan tâm lên CAN → **im lặng tuyệt đối**, không phân biệt
được với một bus khoẻ mạnh. SOME/IP trả lời kèm lý do:

| Mã | Nghĩa |
|---|---|
| `E_UNKNOWN_SERVICE` | không có dịch vụ này ở đây |
| `E_UNKNOWN_METHOD` | có dịch vụ, **không có hàm đó** |
| `E_NOT_READY` | có, nhưng chưa sẵn sàng |
| `E_WRONG_INTERFACE_VERSION` | phiên bản giao diện không khớp |

### Và một loại lỗi hoàn toàn mới: TIMEOUT

Trên bus không có câu hỏi nào đang treo, nên **không có gì để hết giờ**. Ở đây
thì có — và nó **mơ hồ**: client không phân biệt được request mất, reply mất,
hay server chết. **Ba nguyên nhân, một triệu chứng.**

### Nó là hợp đồng giữa các tổ chức

Service id được **sinh ra** từ ARXML hoặc Franca `.fidl`, không ai gõ tay, vì
client và server do hai công ty khác nhau làm. Đúng vai trò mà DBC đóng cho
CAN.

Đó cũng là lý do `vehicle_service.hpp` nằm trong `av_service` chứ không phải
một header `apps/common/`. Tuần 6 xoá `apps/common` vì nó **trùng lặp** với
DBC; đây là trường hợp ngược lại — không file nào khác nói ra những con số này.

---

## 3. Kiến trúc

```text
┌─────── svc_client ────────┐              ┌─────── svc_server ────────┐
│                           │              │                           │
│ socket A  không bind      │──REQUEST────►│                           │
│           port tạm        │◄──RESPONSE───│  socket bound :30509      │
│                           │              │                           │
│ socket B  bound :30510    │◄─NOTIFICATION│  (vẫn socket đó)          │
│           join 239.10.0.2 │              │                           │
└───────────────────────────┘              └───────────────────────────┘

    method   unicast    client ──► server ──► client    CÓ NGƯỜI HỎI
    event    multicast  server ──► group               KHÔNG AI HỎI
```

### Vì sao client cần 2 socket mà server chỉ cần 1

**Server** chỉ **gửi** ra ngoài: response gửi tới `result.from` — địa chỉ đến
từ *chính gói tin nhận được*; event gửi tới group — địa chỉ *hằng số*. Cả hai
đều là `sendto()`, nên một socket là đủ.

**Client** phải **nhận** ở hai chỗ khác nhau:

```
response  → về port tạm của socket A   (kernel tự gán, không bind)
event     → về port 30510 của group    (phải bind + join)
```

Một socket không thể vừa để trống port cho reply, vừa bind cứng vào 30510.

Nhét chung một socket vẫn chạy được. Tách ra để **sự khác biệt nằm trong code**
chứ không chỉ nằm trong comment.

---

## 4. Cài đặt C++ / Linux

### Reply vọng lại câu hỏi

```cpp
Header reply = request;      // copy TOÀN BỘ
reply.type = type;           // rồi chỉ đổi đúng 3 thứ
reply.code = code;
reply.interface_version = version;
```

`service_id`, `method_id`, `client_id`, `session_id` được **vọng lại**, vì
chúng **định danh câu hỏi**. Chỉ `type`, `code` và payload mới là câu trả lời.
Server **không bao giờ tự nghĩ ra** session id.

### So khớp session — mấu chốt của cả tuần

```cpp
if (!pending || pending->session != message.header.session_id) {
    // "IGNORED: no call is waiting for this (late reply)"
    return;
}
```

Không có nó, một reply đến muộn cho câu hỏi **đã bỏ** sẽ được gán cho câu hỏi
**tiếp theo**: bạn hỏi tốc độ lúc `t=5s`, nhận giá trị của `t=3s`, và tin đó là
mới.

> Đó chính là gói stale của tuần 7, đội lốt khác. **Session ID là sequence
> number của tuần 7, được nâng lên thành chuẩn.**

`SessionCounter` wrap về **1**, không về 0, vì 0 là mã dành riêng nghĩa
*"endpoint này không theo dõi session"*. Wrap về 0 là lặng lẽ thông báo mình đã
ngừng đảm bảo thứ tự.

### Reader trả `optional`, không bao giờ trả giá trị mặc định

```cpp
std::optional<float> get_f32() noexcept;
```

Payload cụt là chuyện **bình thường** khi nhận từ mạng. Một reader trả 0 cho
"thiếu dữ liệu" thì không phân biệt được với một reader đọc được số 0 thật.

### `deserialize` từ chối chứ không đoán

Buffer ngắn, protocol version lạ, message type ngoài 5 loại (`0x20` là
SOME/IP-TP — một loại có thật, nhưng là **một mảnh**, không phải message trọn
vẹn), và Length không khớp với số byte thực nhận.

Kiểm tra cuối cùng chính là nơi lỗi Length của đối tác lộ ra — **ở phía nhận**,
thay vì lòi ra ba tầng trên dưới dạng "payload trông như rác".

### Lỗi mà test ghim-từng-byte đã bắt được

`deserialize` của tôi đọc protocol version ở offset **8** và message type ở
**10** — cả hai đều nằm trong vùng **Request ID**.

Test round-trip thuần sẽ **PASS**, vì encode và decode cùng sai theo một kiểu.
Chỉ có ghim byte mới bắt được:

```cpp
0x00, 0x00, 0x00, 0x0C,  // length = 8 + 4 payload  <- NOT 20, NOT 4
```

Đó là lập luận vì sao wire format **phải test bằng byte**, không được test bằng
round-trip.

---

## 5. Bài thực hành

```bash
# terminal 1
./build/debug/apps/svc_server/svc_server

# terminal 2
./build/debug/apps/svc_client/svc_client
```

```text
[cli] --> 0x0001()  session=    2
[cli] <-- RESPONSE   176.0   (85 us)
[cli]  ~  EVENT      176.5   session=53  (nobody asked)
[cli]  ~  EVENT      179.0   session=54  (nobody asked)

calls made     : 4
answered       : 4
events received: 8   <- these needed no call at all
```

Dòng `-->` và dòng `~` là **hai thế giới khác nhau** dùng chung một wire
format. Rồi thử tiếp:

```bash
./build/debug/apps/svc_client/svc_client --method 0x0009   # E_UNKNOWN_METHOD
./build/debug/apps/svc_client/svc_client --events-only     # chỉ nghe, không hỏi
./build/debug/apps/svc_server/svc_server --no-events       # chỉ method
./build/debug/apps/svc_server/svc_server --wrong-version   # payload không giải mã
```

Và tắt hẳn server đi:

```text
[cli] !!! TIMEOUT  session=    2  after 300 ms -- no answer, and no way to know why
timed out      : 3
```

---

## 6. Bài lỗi / gỡ lỗi

| Lỗi | Gây ra bằng | Client thấy gì | Bằng chứng ở đâu |
|---|---|---|---|
| Hàm không được cài đặt | `--method 0x0009` | `ERROR E_UNKNOWN_METHOD` trong ~60 us | nhánh `default` của `switch` bên server |
| Sai dịch vụ trên port đó | service id khác | `ERROR E_UNKNOWN_SERVICE` | kiểm tra đầu tiên của server |
| Giao diện đổi | `svc_server --wrong-version` | có response, **payload KHÔNG giải mã** | version lệch = layout không còn được thoả thuận |
| Server chết | tắt `svc_server` | `TIMEOUT` 300 ms, 3 nguyên nhân không phân biệt được | không có gì — **đó chính là vấn đề** |
| Reply đến muộn | tăng tải | `IGNORED: no call is waiting for this` | session id không khớp `pending` |
| Length tính cả message | lỗi của đối tác | `length field disagrees with the bytes received` | `deserialize`, phía nhận |
| Event im lặng | `svc_server --no-events` | method vẫn chạy, dòng `~` biến mất | chứng minh hai đường độc lập nhau |

Quy trình truy vết (CLAUDE.md §9) cho *"cluster hiện tốc độ cũ"*:

```text
server có phát không?         stdout của chính nó
có rời khỏi máy không?        tcpdump -i lo -n port 30510
đã join group chưa?           ip maddr show
có parse được không?          dòng warn "not a SOME/IP message"
đúng message type chưa?       Notification hay Response
session có tăng không?        session id in ra mỗi event
có giải mã được không?        payload đủ dài để chứa float chưa
```

---

## 7. Câu hỏi phỏng vấn

1. Giao thức hướng dịch vụ diễn đạt được gì mà bus hướng tín hiệu không? Cho ví
   dụ cockpit cụ thể.
2. SOME/IP hay UDP — chọn cái nào? (Câu này là bẫy; giải thích vì sao.)
3. SOME/IP dùng UDP khi nào, TCP khi nào, và lý do là gì?
4. Length đếm chính xác cái gì? Đối tác tính sai thì chuyện gì xảy ra, và lỗi
   lộ ra đầu tiên ở đâu?
5. Vì sao toàn bộ big-endian, và chỗ nào trong repo này va chạm với quy ước
   ngược lại?
6. Session ID để làm gì? Liên hệ với tuần 7.
7. Vì sao `SessionCounter` wrap về 1 chứ không về 0?
8. Client gửi 3 request, nhận 1 reply. Làm sao biết nó trả lời request nào?
9. Phân biệt `E_UNKNOWN_SERVICE`, `E_UNKNOWN_METHOD` và timeout. Cái nào mơ hồ,
   và vì sao điều đó quan trọng?
10. Vì sao client mở 2 socket mà server chỉ mở 1?
11. Vì sao version lệch thì phải **dừng** giải mã payload, chứ không giải mã
    đại?
12. Vì sao test wire format bằng cách ghim byte thay vì round-trip?

---

## 8. Checklist tự kiểm

- [ ] Vẽ được header 16 byte từ trí nhớ, kể cả Length đếm cái gì.
- [ ] Giải thích được vì sao "SOME/IP hay UDP" là câu hỏi sai.
- [ ] Nói được quy tắc chọn CAN / Ethernet, kèm 2 ví dụ mỗi bên.
- [ ] Nói được lập luận "lúc khởi động" cho method, trong một câu.
- [ ] Giải thích được Session ID bằng cách liên hệ với sequence number tuần 7.
- [ ] Kể được 3 return code và mỗi cái loại trừ điều gì.
- [ ] Giải thích được vì sao timeout mơ hồ, và điều đó ép ứng dụng phải làm gì.
- [ ] **Đã tự chạy client với `--method 0x0009` và với server đã tắt.**

---

**Còn nợ sau tuần 8:** đọc code của một stack thật. ROADMAP chỉ định `vsomeip`
(COVESA), và ghi chú này viết từ đặc tả cùng bản cài đặt ở đây, **không phải**
từ mã nguồn đó. Nên đọc trước khi làm ghi chú thiết kế middleware của tuần 10,
vì routing manager của vsomeip chính là quyết định thiết kế mà repo này chưa
phải đối mặt.

**Tiếp theo — tuần 9:** hiện tại client biết server ở `127.0.0.1:30509` vì
`vehicle_service.hpp` viết cứng như vậy. Trên xe thật thì hỏng ngay khi một ECU
khởi động chậm, đổi địa chỉ, hoặc không được lắp. Service Discovery thay hằng
số đó bằng `OfferService` / `FindService` — và kéo theo cả availability,
timeout và retry.
