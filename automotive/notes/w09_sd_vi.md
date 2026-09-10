# Tuần 9 — Service Discovery: xoá địa chỉ khỏi code

## 1. Khái niệm

Client tuần 8 chạy được vì có một header nói cho nó biết phải đi đâu:

```cpp
inline constexpr std::uint16_t kMethodPort = 30509;      // vehicle_service.hpp
inline constexpr const char*   kEventGroup = "239.10.0.2";
```

Đó là một *niềm tin biên dịch sẵn* về thế giới. Service Discovery thay nó bằng
một cuộc hội thoại:

```text
server ──OfferService (lặp lại, TTL 3 s)──────► 239.10.0.9:30490
client ──FindService──────────────────────────► 239.10.0.9:30490
server ──OfferService (unicast, ngay lập tức)─► đúng client vừa hỏi
```

Lời offer **mang theo endpoint**, nên client *học* được thay vì được biên dịch
cùng. Đúng một địa chỉ vẫn phải thống nhất trước — chính SD group, vì discovery
không thể tự discover chính nó — và địa chỉ đó giống nhau trên mọi xe, cho mọi
service.

### Lease mới là ý chính

Một offer không phải là *sự thật*. Nó là **lease** có hiệu lực TTL giây, và
server phải liên tục gia hạn.

```text
t=0.0  OfferService ttl=3     client: AVAILABLE
t=1.0  OfferService ttl=3     lease được gia hạn
t=2.0  OfferService ttl=3     lease được gia hạn
       ...server bị kill, và không nói gì cả...
t=5.0  (không có gì đến)      client: UNAVAILABLE
```

Ở t=5.0 **không có gói tin nào được gửi**. Client biết server chết **từ sự im
lặng** — điều mà không một message nào diễn đạt được. Đây là bộ phát hiện lỗi
duy nhất trong hệ thống còn hoạt động khi peer *vắng mặt* chứ không chỉ *sai*.
Và đó chính là thứ timeout của tuần 8 không làm được.

Gia hạn lease **chính là** heartbeat. Không cần giao thức liveness riêng, và
cũng không nên có.

### Bản tin

SD là một SOME/IP Notification bình thường: service `0xFFFF`, method `0x8100`.
`serialize()`/`deserialize()` của tuần 8 chở nó mà không cần trường hợp đặc
biệt nào — chỉ có payload là mới.

```text
┌─ SD payload ───────────────────────────────────────────────┐
│ Flags (1)  bit7 Reboot, bit6 Unicast    │ Reserved (3)      │
│ Length of Entries Array (4)                                 │
│ Entry [16] · Entry [16] · ...                               │
│ Length of Options Array (4)                                 │
│ Option [12] · Option [12] · ...                             │
└─────────────────────────────────────────────────────────────┘
```

Entry và option nằm ở hai mảng riêng vì nhiều entry thường dùng chung một
endpoint. Entry trỏ vào mảng option bằng **index và count** — count chỉ 4 bit,
hai cái gói chung trong một byte.

## 2. Vì sao Cockpit DC cần nó

Ba tình huống, đều rất đời thường, và không cái nào giải được bằng hằng số:

| Tình huống | Không có SD | Có SD |
|---|---|---|
| ECU khởi động sau cluster | gọi vào khoảng không; timeout không phân biệt được "chưa" với "không bao giờ" | client ở trạng thái SEARCHING, không gọi gì, rồi tìm thấy |
| ECU đổi chỗ — board mới, địa chỉ mới | biên dịch lại mọi client | không đổi gì; offer nói nó đang ở đâu |
| Bản xe không lắp màn hình sau | timeout vĩnh viễn với một thứ không tồn tại | service đơn giản là không bao giờ được offer |

Cái thứ ba mới là lập luận trụ được trong phỏng vấn. Một dây chuyền xe làm hàng
chục biến thể từ **một** image phần mềm. *Service nào tồn tại* là một sự kiện
**triển khai**, và sự kiện triển khai thì không được là hằng số biên dịch.

Vì vậy `vehicle_service.hpp` giờ có một đường kẻ ngang:

```text
trên đường kẻ    service id, method id, interface version   → hợp đồng
dưới đường kẻ    port, multicast group                      → triển khai
```

Các id giống nhau trên mọi xe và không ai đi discover chúng. Địa chỉ thì không,
và server là process duy nhất thực sự biết địa chỉ của chính nó.

**Availability điều khiển tài nguyên, không chỉ dòng log.** Khi event group là
thứ *học được* chứ không phải biên dịch sẵn, thì IGMP membership thuộc về
service: join khi service xuất hiện, drop khi lease hết. Một subscription sống
lâu hơn service của nó là một switch đang flood vào cổng không ai đọc.

## 3. Kiến trúc

```text
┌──────── svc_client ────────┐              ┌──────── svc_server ───────┐
│ SD socket :30490           │──FindService─►                           │
│   joined 239.10.0.9        │◄─OfferService─  SD socket :30490         │
│                            │              │   offer mỗi 1 s, TTL 3    │
│ socket A  không bind       │──REQUEST────►│                           │
│   (ephemeral, nhận reply)  │◄──RESPONSE───│  socket :30509            │
│                            │              │                           │
│ socket B  CHỈ được tạo khi │◄─NOTIFICATION│  (cùng socket đó)         │
│   một offer chỉ ra group   │              │                           │
└────────────────────────────┘              └───────────────────────────┘
```

Sự bất đối xứng của tuần 8 vẫn còn — server một socket traffic, client hai — và
SD thêm cái thứ ba cho mỗi bên. Điểm mới là **vòng đời** của socket B: nó không
tồn tại lúc khởi động, được tạo khi một offer chỉ ra group, và bị huỷ khi lease
kết thúc.

Trạng thái của client:

```text
        ┌──────────► SEARCHING ──────────┐
        │        (FindService, 1 Hz)     │  offer đến
   lease hết hạn                         ▼
   hoặc StopOffer ◄────────────────── AVAILABLE
                                (gọi, subscribe, nghe)
```

Không gọi method nào khi đang SEARCHING. Điều đó **thu hẹp ý nghĩa** của
timeout: tuần 8 timeout còn bao gồm cả "server không tồn tại"; ở đây registry
đã xác nhận service *có* được offer, nên timeout nghĩa là mất datagram hoặc
server treo. Ít nguyên nhân hơn chính là thứ làm một thông báo lỗi có giá trị.

## 4. Hiện thực C++ / Linux

### Cái bẫy length, lần thứ hai

```cpp
constexpr std::uint16_t kIpv4OptionLength = 9;   // trong khi option chiếm 12 byte
```

Trường Length không đếm chính nó, cũng không đếm byte Type ngay sau nó — nên
một option dài `declared + 3` byte. Tuần 8 có đúng dạng bug này (Length của
SOME/IP đếm từ Request ID). Sai chỗ này **vô hình** trong round-trip test, và
cũng vô hình khi chỉ có một option: option *đầu tiên* parse đúng, mọi option
sau nó lệch hết.

Vì vậy test ghim từng byte:

```cpp
0x00, 0x09,              // length 9  <- option chiếm 12
0x04,                    // IPv4 unicast endpoint
0x00,                    // reserved
0x7F, 0x00, 0x00, 0x01,  // 127.0.0.1
0x00,                    // reserved
0x11,                    // L4 = UDP
0x77, 0x2D,              // port 30509
```

### Một entry, hai loại option

`0x04` là unicast endpoint, `0x14` là multicast group — hai **loại option khác
nhau**, không phải một cờ. Đó là cách một offer nói "gọi tôi ở đây, nghe event
của tôi ở kia" trong cùng một entry, và là cách client học được **cả hai** hằng
số của tuần 8 cùng lúc.

### Registry tách byte khỏi ngữ nghĩa

```cpp
Availability observe(const Entry& entry, Clock::time_point now);
std::vector<Change> expire(Clock::time_point now);
```

Đồng hồ là **tham số**, không phải lời gọi `now()` bên trong. Nhờ vậy chuyện
lease hết hạn được test trong vài micro giây thay vì ngồi ngủ 3 giây — mà một
test phải chờ 3 giây là test không ai chạy.

Gia hạn trả về `Unchanged`, không phải `Available`. Nếu báo tin mỗi lần offer
lặp lại, client sẽ chạy lại logic "service vừa lên" hai lần mỗi giây.

### TTL gấp ba lần chu kỳ

```cpp
constexpr auto kOfferInterval = std::chrono::seconds{1};
constexpr std::uint32_t kOfferTtl = 3;
```

Nếu bằng nhau, mất **một** offer là lease hết hạn và mọi client tưởng server
chết. Gấp ba thì chịu được hai lần mất liên tiếp. Lớn hơn nữa thì một cú crash
thật mất tỉ lệ thuận lâu hơn để phát hiện. Toàn bộ đánh đổi
availability/responsiveness nằm ở đúng một con số này.

### Bug mà việc **chạy** tìm ra, còn đọc thì không

Phát hiện reboot so cờ reboot với session id cuối cùng thấy từ một peer. Phiên
bản đầu giữ **một** biến đếm và áp cho mọi bản tin SD. Nhưng client join SD
group với loopback bật, nên nó nhận lại chính `FindService` của mình — và đem
session id của chính nó so với của server:

```text
[cli]  !  REBOOT  127.0.0.1 restarted: session went 2 -> 1
```

in ra **trước khi** offer đầu tiên kịp đến. Hai cách sửa, cả hai đều có nguyên
tắc: trạng thái giữ **theo từng sender**, và chỉ theo dõi bản tin thực sự
**offer** cái gì đó. Một câu hỏi không mang trạng thái nào đáng nhớ.

Đây đúng là cái bẫy `udp_socket.hpp` đã cảnh báo từ tuần 7 — *"một chương trình
vừa publish vừa subscribe sẽ thấy traffic của chính nó và tưởng là của peer"* —
gặp thật sau hai tuần.

### Riêng cờ reboot không chứng minh được gì

Nó nghĩa là "session id của tôi chưa wrap lần nào", điều vẫn đúng với một server
đã chạy suốt một tiếng. Khởi động lại = cờ đó **cộng với** một session id đi
lùi.

## 5. Bài thực hành

Chạy **client trước**. Đó chính là điểm mấu chốt.

```bash
# terminal 1
./build/debug/apps/svc_client/svc_client

# terminal 2, vài giây sau
./build/debug/apps/svc_server/svc_server
```

```text
[cli]  ?  FindService  service=0x1234  -> 239.10.0.9:30490   (still searching)
[cli]  ?  FindService  service=0x1234  -> 239.10.0.9:30490   (still searching)
[cli] +++ AVAILABLE    methods at 127.0.0.1:30509   events at 239.10.0.2:30510
[cli]  +  subscribed to 239.10.0.2:30510   (IGMP join)
[cli] --> 0x0001()  session=    1  to 127.0.0.1:30509
[cli] <-- RESPONSE    41.0   (71 us)
```

Rồi so sánh **hai cách** một service biến mất.

```text
# Ctrl-C server -- chào tử tế
[cli] !!! UNAVAILABLE  StopOffer received -- server chủ động rút service
[cli]  -  left 239.10.0.2:30510

# kill -9 server -- không chào gì cả
[cli] !!! TIMEOUT  session=8  after 300 ms
[cli] !!! TIMEOUT  session=9  after 300 ms
[cli] !!! UNAVAILABLE  service=0x1234 -- lease hết hạn, không ai nói lời tạm biệt
```

| | độ trễ nhận biết | số lệnh gọi hỏng |
|---|---|---|
| StopOffer | ngay lập tức | **0** |
| crash | ~3 s (đúng bằng TTL) | **2** |

Bảng này **là** cả tuần 9. Bật lại server sau đó và xem:

```text
[cli]  !  REBOOT  127.0.0.1:30490 restarted: session went 5 -> 1
```

Sau đó:

```bash
./build/debug/apps/svc_client/svc_client --static     # tuần 8: tin vào hằng số
./build/debug/apps/svc_server/svc_server --no-sd      # tồn tại, nhưng không bao giờ offer
```

`--static` gặp `--no-sd` thì chạy được, vì cả hai bên được build với cùng bộ
số. Client thường gặp `--no-sd` thì tìm mãi mãi — **đúng**, vì đứng từ phía
mạng mà nói, service đó không có ở đó.

## 6. Bài gây lỗi / gỡ lỗi

| Lỗi | Cách tạo | Client thấy gì | Bằng chứng ở đâu |
|---|---|---|---|
| Service không được offer | `svc_server --no-sd` | `FindService` mãi, không gọi gì | không có gì để xem — đó chính là câu trả lời |
| Tắt sạch | Ctrl-C server | `UNAVAILABLE` ngay, 0 timeout | StopOffer, TTL 0 |
| Crash | `kill -9` server | 2 timeout, rồi `UNAVAILABLE` | lease, không phải một bản tin |
| Server khởi động lại | tắt rồi bật | `REBOOT`, session đi lùi | cờ reboot **và** session id |
| Sai Length của option | bug của peer | `IPv4 option with the wrong length` | `parse_options`, ở phía nhận |
| Entry trỏ vào option không tồn tại | bug index/count của peer | `entry references options that are not there` | entry bị bỏ, cả bản tin thì không |
| Loại option lạ | ECU nhà cung cấp bản mới | không gì cả — bị bỏ qua | cố ý: từ chối nó là hỏng đúng hôm họ nâng cấp |
| Membership sống lâu hơn service | xoá lời gọi `unsubscribe` | event vẫn đến từ một service đã chết | `ip maddr show` vẫn liệt kê group |

Kỷ luật truy vết (CLAUDE.md §9) cho "cluster không bao giờ tìm thấy service tốc
độ":

```text
có ai đang offer không?         tcpdump -i lo -n port 30490
client có hỏi không?            chính dòng "FindService" của nó
cùng SD group chứ?              ip maddr show  -- 239.10.0.9 ở cả hai
có parse ra SD không?           service id 0xFFFF, method 0x8100
service id trong entry đúng?    0x1234, và instance
TTL hợp lý?                     0 là StopOffer, không phải một offer
option resolve được?            "entry references options that are not there"
endpoint có tới được?           địa chỉ trong offer, không phải địa chỉ bạn đoán
```

Để ý dòng cuối. Có SD rồi thì "địa chỉ bạn đoán" hết là thứ cần kiểm tra, và
"địa chỉ nó quảng bá" mới là thứ cần kiểm tra.

## 7. Câu hỏi phỏng vấn senior

1. Vì sao endpoint biên dịch sẵn chấp nhận được trong lab nhưng không chấp nhận
   được trên xe? Nêu ba tình huống hỏng khác nhau.
2. Vì sao offer là một *lease* chứ không phải một *sự thật*? TTL mua được gì mà
   một thông báo một lần không mua được?
3. Client làm sao biết server đã crash, khi mà theo định nghĩa crash thì không
   gửi gì cả?
4. StopOffer so với lease hết hạn: cùng kết quả, khác chi phí. Định lượng khác
   biệt đó từ demo.
5. Bạn chọn chu kỳ offer và TTL thế nào? Tỉ lệ giữa chúng đánh đổi cái gì?
6. Trường Length của SD option đếm cái gì, và khi peer làm sai thì lỗi lộ ra
   đầu tiên ở đâu?
7. Cờ reboot dùng để làm gì, và vì sao riêng nó là không đủ?
8. Vì sao trạng thái reboot phải giữ theo từng sender? Không làm vậy thì hỏng
   gì?
9. Cái gì thuộc về một interface header sinh tự động, cái gì không? Bảo vệ
   đường kẻ đã vạch trong `vehicle_service.hpp`.
10. Vì sao event socket của client không tồn tại lúc khởi động?
11. Vì sao chuẩn cho phép tăng dần nhịp retry FindService thay vì dùng chu kỳ
    cố định?
12. Có SD đứng trước, timeout của một request giờ mang nghĩa gì mà tuần 8 không
    có?

## 8. Checklist ôn tập

- [ ] Giải thích được vì sao cần SD, bằng một câu về biến thể xe.
- [ ] Vẽ được SD payload: flags, mảng entry, mảng option.
- [ ] Giải thích được index/count của entry trỏ vào đâu.
- [ ] Nói được trường Length của option đếm gì, sai thì hỏng ra sao.
- [ ] Giải thích được lease, và vì sao im lặng là bộ phát hiện crash duy nhất.
- [ ] Nêu được tỉ lệ chu kỳ/TTL và bảo vệ được con số đó.
- [ ] Giải thích được StopOffer và định lượng được nó tiết kiệm gì.
- [ ] Giải thích được phát hiện reboot và vì sao riêng cờ là không đủ.
- [ ] Nói được timeout giờ mang nghĩa gì khi đã có discovery đứng trước.
- [ ] Đã chạy client trước server, và đã giết server theo cả hai cách.

---

**Còn treo cuối tuần 9.** Hai thứ tuần này *có gọi tên* nhưng *không* hiện
thực, ghi lại để khoảng trống này không bị nhầm là đã xong:

- **SubscribeEventgroup / SubscribeEventgroupAck.** Hai entry type có trong
  `sd.hpp` và được parse, nhưng client này subscribe bằng cách tự join
  multicast group — server không hề biết. Một stack đầy đủ thì client *xin*
  subscribe và server *ack*, và đó là thứ cho phép có đường event TCP riêng cho
  từng client, cùng với kiểm soát truy cập ở mức eventgroup.
- **`vsomeip` (COVESA), vẫn hoãn.** Mang từ tuần 8 sang. Routing manager của nó
  chính là quyết định thiết kế mà repo này chưa phải đối mặt: **một** SD daemon
  cho mỗi ECU mà mọi process nói chuyện cục bộ, thay vì mỗi process tự mở
  socket SD như hai chương trình ở đây. Đó là cửa vào tự nhiên của tuần 10.

**Tiếp theo — tuần 10:** middleware architecture. Ba tuần vừa rồi đẻ ra một
wire format, một service, và một cách tìm ra nó — dưới dạng ba chương trình mà
mỗi cái tự dựng socket của mình. Tuần 10 hỏi **ranh giới** nên là gì: một ứng
dụng nhìn thấy cái gì, ai sở hữu thread, chuyện gì xảy ra khi backpressure, và
chính sách timeout/retry sống ở đâu. Rồi `av_service` được refactor cho khớp
với câu trả lời.
