# Tuần 7 — Automotive Ethernet: đổi sự chắc chắn lấy băng thông

> Bản tiếng Việt của [`w07_ethernet.md`](w07_ethernet.md). Thuật ngữ tiếng Anh
> được giữ nguyên, vì đó là chữ bạn sẽ gặp trong datasheet, trong code và trong
> phỏng vấn.

---

## 1. Khái niệm

### Vấn đề: CAN hết chỗ

CAN có một trần cứng: **1 Mbit/s, 8 byte mỗi frame** (CAN-FD: ~5 Mbit/s, 64
byte). Với *"tốc độ là 87.5 km/h"* thì thừa sức, và CAN sẽ không biến mất.

Nhưng những thứ mới trên xe thì không vừa:

| Thứ trên xe | Cần bao nhiêu |
|---|---|
| Camera lùi (đã nén) | ~5–20 Mbit/s |
| Camera 360° (4 cái) | hàng trăm Mbit/s |
| Radar / LiDAR cho ADAS | hàng chục Mbit/s |
| Cập nhật OTA cho IVI | 4 GB |

Tính thử cái cuối: 4 GB qua CAN 1 Mbit/s ≈ **hơn 18 giờ**. Qua Ethernet
100 Mbit/s ≈ **6 phút**.

> **Xe hiện đại có CẢ HAI.** CAN chở tín hiệu nhỏ, ngặt về thời gian.
> Ethernet chở dữ liệu lớn. Ai nói "Ethernet thay thế CAN" là hiểu ngược kiến
> trúc.

### Cái giá: bốn bảo đảm biến mất

Đây là **toàn bộ tuần 7** gói trong một bảng:

| CAN cho không | Ai đảm bảo | Trên Ethernet |
|---|---|---|
| Frame đến **đúng thứ tự** | một bus vật lý duy nhất | switch có thể đảo thứ tự |
| Frame **retry đến khi có ACK** | controller, bằng phần cứng | datagram bị vứt **trong im lặng** |
| Ưu tiên cưỡng chế **trên từng bit** | arbitration, bằng vật lý | DSCP/PCP chỉ là *gợi ý* switch được phép bỏ qua |
| **Mọi node đều nghe thấy** | bus là môi trường quảng bá | switch chỉ chuyển tới nơi nó *tin* có người nghe |

**Mọi middleware ô tô — SOME/IP, DDS, AUTOSAR Adaptive — tồn tại để mua lại
bốn thứ đó bằng phần mềm.** Ai chưa từng thấy chúng biến mất sẽ coi sequence
number, timeout và retry của tuần 8 là nghi lễ vô nghĩa.

### Tầng vật lý cũng khác

Cáp mạng văn phòng có **4 đôi dây xoắn**, có vỏ chống nhiễu, đầu RJ45. Trên xe
thì không chấp nhận được:

- Bó dây điện là bộ phận **nặng thứ ba** sau động cơ và thân xe
- RJ45 không sống nổi với rung, dầu, −40 °C đến +85 °C
- Nhiễu điện từ bẩn hơn văn phòng rất nhiều (bugi, motor, inverter)

| 100BASE-TX (văn phòng) | 100BASE-T1 (ô tô) |
|---|---|
| 4 đôi dây, có vỏ | **1 đôi dây, KHÔNG vỏ** |
| RJ45 | đầu nối nhỏ, kín, chịu rung |
| có thể dùng hub, dùng bus | **ĐIỂM–ĐIỂM nghiêm ngặt** |
| nguồn riêng hoặc PoE | PoDL — cấp nguồn trên chính đôi dây đó |
| 100 m | 15 m |

**Dòng quan trọng nhất là "điểm–điểm nghiêm ngặt".** CAN là *bus*: cắm thêm
ECU là xong, ai cũng nghe thấy. 100BASE-T1 nối **đúng hai** thiết bị, nên ba
ECU muốn nói chuyện thì **bắt buộc** phải có **switch**.

Và cái switch đó chính là nơi sinh ra đảo thứ tự, mất gói im lặng, và ưu tiên
bị bỏ qua.

---

## 2. Vì sao nó tồn tại trong Cockpit DC

- **Là xương sống mà cockpit treo lên.** Camera, ADAS domain controller, màn
  hình ghế sau, cổng chẩn đoán — tất cả đến qua Ethernet. CAN đến cockpit để
  mang tín hiệu; Ethernet mang mọi thứ còn lại.

- **Nó đổi hình dạng của bug.** Lỗi tuần 4 tự khai báo: bus-off, TEC/REC tăng,
  error frame trên dây. Lỗi tuần 7 thì **im lặng** — join sai NIC, switch không
  bật IGMP snooping, DSCP bị xoá dọc đường, gói này vượt mặt gói kia. Không
  throw, không log, test vẫn xanh.

- **Là tầng mà SOME/IP nằm lên.** Tuần 8 lo tuần tự hoá và ngữ nghĩa dịch vụ;
  nó *giả định* đã có sẵn một đường ống datagram. Xây ống trước giữ hai câu hỏi
  tách bạch: *"byte tới ECU khác bằng cách nào"* khác với *"byte đó nghĩa là
  gì"*.

- **Multicast chính là publish/subscribe ở tầng dưới.** Sự tách rời mà mô hình
  event của SOME/IP cho bạn ở tầng dịch vụ, IP multicast đã cho ở tầng mạng.
  Gặp hai lần thì lần thứ hai trở nên hiển nhiên.

---

## 3. Kiến trúc

```text
                      tín hiệu xe
                             │
                        dbc/cockpit.dbc  ── 1 file, 3 người đọc ──────┐
                             │                                        │
                    ┌────────▼────────┐                               │
                    │     eth_gw      │  CanFrame                     │
                    │                 │      │                        │
                    │                 │  tunnel_encode                │
                    │                 │      │  + sequence number     │
                    └────────┬────────┘                               │
                             │ sendto()                               │
                    239.10.0.1:30490                                  │
                       TTL 1, DSCP 46                                 │
                             │                                        │
              ┌──────────────┼──────────────┐                         │
              │              │              │   switch chỉ nhân bản   │
              ▼              ▼              ▼   tới cổng nào đã gửi   │
          eth_sub        eth_sub        (logger)  IGMP membership     │
        (cluster)         (IVI)                        report         │
              │                                                       │
        tunnel_decode ──► SequenceTracker ──► giải mã DBC ◄───────────┘
                                 │
                     Gap / Stale / Duplicate
```

Sơ đồ này nói hai điều:

1. **Publisher không biết ai đang nghe.** Nó gửi một lần tới địa chỉ nhóm. Thêm
   người nghe thứ tư: `eth_gw` không đổi một dòng nào.
2. **Sequence number là thứ DUY NHẤT mang thông tin về thứ tự.** Không phải
   switch, không phải UDP, không phải kernel.

### Vì sao UDP, mà không phải TCP hay Ethernet thô

**Ethernet thô** chỉ có địa chỉ MAC — đủ để tới đúng *máy*, không đủ để tới
đúng *chương trình*. Số cổng (port) là thứ cho phép cluster, IVI và logger mỗi
bên nhận luồng riêng trên cùng một NIC. Đi qua IP còn được thêm: multicast,
IGMP, và mọi công cụ chuẩn (`tcpdump`, Wireshark).

**TCP** là câu trả lời sai nhưng hấp dẫn. Xét một gói bị mất trong luồng 10 Hz:

```text
TCP                                     UDP
t=0.0  A (40 km/h)  ── MẤT ──✗          A mất, không bao giờ thấy
t=0.1  B tới → NẰM CHỜ trong kernel     B giao ngay lúc t=0.1
t=0.2  C tới → NẰM CHỜ                  C giao ngay lúc t=0.2
t=0.3  A được gửi lại, tới nơi
       → A,B,C,D giao cùng một lúc
```

**Head-of-line blocking.** B, C, D đã nằm trong máy người nhận suốt thời gian
đó, nhưng TCP không giao vì phải giữ đúng thứ tự bằng mọi giá. Trên cụm đồng
hồ: **kim tốc độ đứng hình 300 ms rồi nhảy**.

Bên dưới còn một lý do sâu hơn:

> **Tốc độ lúc `t=0.0` không còn giá trị gì ở thời điểm `t=0.3`.**

Gửi lại là dịch vụ *sai* cho dữ liệu chu kỳ. Chuyển file cần đúng byte bị mất;
luồng tín hiệu cần **mẫu tiếp theo**. Và TCP là một-đối-một — multicast không
tồn tại trên TCP.

Vậy nên: **UDP để phát hiện, không phải để sửa.** Mua thứ mình cần (biết gói bị
mất hay bị đảo), không mua thứ mình không cần (nhận lại một giá trị đã cũ).

---

## 4. Cài đặt C++ / Linux

Ba mảnh, xếp tầng có chủ đích, mỗi mảnh trả lời một câu hỏi.

### `av_eth::UdpSocket` — byte tới ECU khác bằng cách nào

`socket(AF_INET, SOCK_DGRAM)`, rồi bốn **quyết định riêng biệt**:

| Hàm | Syscall | Quyết định |
|---|---|---|
| `bind_any(port)` | `SO_REUSEADDR` rồi `bind` | socket này sở hữu port nào |
| `join_multicast(group, iface)` | `IP_ADD_MEMBERSHIP` | nhóm nào, **và qua NIC nào** |
| `set_multicast_interface(iface)` | `IP_MULTICAST_IF` | multicast *đi ra* qua NIC nào |
| `set_multicast_ttl(1)` | `IP_MULTICAST_TTL` | không bao giờ rời khỏi local link |
| `set_dscp(46)` | `IP_TOS`, giá trị `<< 2` | xin ưu tiên; 2 bit thấp là ECN |

Ba chi tiết đáng bảo vệ:

- **`SO_REUSEADDR` TRƯỚC `bind`, không phải sau** — kernel đọc cờ này *trong
  lúc* bind. Thiếu nó thì subscriber thứ hai trên cùng máy chết với
  `EADDRINUSE`. Hai subscriber trên một máy là trường hợp **bình thường**,
  không phải ngoại lệ.

- **`send_to` từ chối mọi thứ lớn hơn `kMaxDatagram`** (1472 = 1500 − 20 − 8)
  thay vì để IP phân mảnh. Mất **bất kỳ** mảnh nào là mất cả datagram, nên gói
  2 mảnh biến mất với xác suất gần gấp đôi gói 1 mảnh.

- **`set_dscp` LUÔN thành công**, kể cả khi không switch nào tôn trọng nó. Đây
  là tương phản sắc nét nhất với CAN trong cả tuần: arbitration được **vật lý
  cưỡng chế trên từng bit**; DSCP chỉ là một **lời xin** mà mỗi switch được
  quyền tôn trọng, đổi, hoặc xoá sạch.

### `av_can::tunnel_*` — gói nhiều frame kèm sequence number

```text
┌─ header, 16 byte ──────────────────────────────────────────┐
│ 'A''V''E''T' │ ver │ count │ rsvd │ sequence u32 │ ms u32   │
└────────────────────────────────────────────────────────────┘
┌─ record, 16 byte × count ──────────────────────────────────┐
│ id u32 (bit31 = extended) │ len │ flags │ rsvd │ 8 data     │
└────────────────────────────────────────────────────────────┘
```

- **Big-endian trên dây** để ECU big-endian và little-endian hiểu giống nhau mà
  không cần thương lượng. Payload CAN được copy **nguyên xi** — ý nghĩa của nó
  thuộc về DBC, không thuộc về tầng này.

- **Bit 31 làm cờ extended id** — đúng quy ước tuần 6 đã gặp ở dòng `BO_` trong
  DBC. Một luật để nhớ, một lỗi lệch-một để nhận ra.

- **Kiểm tra độ dài khi decode là CHÍNH XÁC, không phải "ít nhất".** Datagram
  là nguyên tử — một `recvfrom` trả về đúng một `send_to` — nên độ dài không
  khớp `count` là gói **hỏng**, không bao giờ là gói *một phần đang chờ phần
  còn lại*. Đây là chỗ tư duy kiểu TCP làm sai.

- **8 byte payload mỗi record, nên CAN-FD không vừa.** Đây là hạn chế được
  **nói ra**, không giấu; tuần 8 thay cả định dạng này bằng tuần tự hoá
  SOME/IP.

### `SequenceTracker` — ba dòng quan trọng nhất

```cpp
const auto delta = static_cast<std::int32_t>(sequence - highest_);
if (delta == 0) { return Duplicate; }
if (delta < 0)  { return Stale; }     // bị đảo — KHÔNG được áp dụng
```

Trừ trước, rồi mới ép sang kiểu có dấu — đó là thứ làm phép so sánh **an toàn
khi wrap**.

`sequence < highest_` trông đúng, và đúng suốt 4.29 tỉ datagram — rồi **sai
vĩnh viễn**: tại điểm wrap, sequence 0 bị đọc là cũ hơn 4294967295, và
receiver lặng lẽ từ chối mọi gói từ đó về sau. Một gateway chạy đủ lâu sẽ đột
nhiên không còn được tin nữa. Không lần chạy test ngắn nào bắt được — đó là lý
do có case `SurvivesTheThirtyTwoBitWrap`.

Gói stale bị **vứt, không phải đệm lại**. Với luồng tín hiệu, một giá trị mới
hơn đã thay thế nó rồi; sắp lại thứ tự chỉ khiến bạn hiển thị thứ cũ hơn, muộn
hơn. Chuyển file thì cần chính sách ngược lại — và đó chính là lý do quyết định
này thuộc về **ứng dụng**, không thuộc về **transport**.

---

## 5. Bài thực hành

```bash
# terminal 1
./build/debug/apps/eth_sub/eth_sub --naive

# terminal 2
./build/debug/apps/eth_gw/eth_gw --reorder 3 --cycles 9
```

Không cần `sudo`, không cần `vcan0`. Tốc độ do gateway sinh ra **chỉ tăng**,
nên mọi lần giảm trên màn hình đều đến từ mạng.

```text
[rx] seq=   3  speed=  41.5  ** datagram(s) LOST **
[rx] seq=   2  speed=  41.0  ** stale, applied anyway **   <<< SPEED WENT BACKWARDS
```

Giờ bỏ `--naive`, chạy lại gateway y hệt:

```text
[rx] seq=   4  speed=  42.0  ** datagram(s) LOST **
[rx] seq=   3                 STALE -- dropped (an older reading arrived late)

speed went backwards: 0
```

**Cùng dữ liệu, cùng thứ tự đến, kết quả ngược nhau.** Chạy hai subscriber cùng
lúc — một thường, một `--naive` — với chung một gateway: hai cửa sổ hiển thị
khác nhau dù nhận **byte giống hệt nhau**. (Việc cả hai cùng bind được port
30490 chính là `SO_REUSEADDR` đang làm việc.)

Rồi đến chuỗi thật, sau khi đã chạy `sudo ./scripts/setup_vcan.sh`:

```bash
./build/debug/apps/sim_vehicle/sim_vehicle vcan0
./build/debug/apps/eth_gw/eth_gw --can vcan0 --reorder 5
./build/debug/apps/eth_sub/eth_sub
```

---

## 6. Bài lỗi / gỡ lỗi

Điểm mấu chốt: **không lỗi nào dưới đây sinh ra thông báo lỗi.**

| Lỗi | Gây ra bằng | Bạn thấy gì | Bằng chứng ở đâu |
|---|---|---|---|
| Áp dụng gói đảo thứ tự | `eth_sub --naive` | tốc độ tụt ngược | `SequenceTracker` đã nói `Stale` — không ai đọc |
| Join sai NIC | `--iface 0.0.0.0` trên máy 2 NIC | im lặng vĩnh viễn | `setsockopt` trả 0; `ip maddr show` cho thấy nhóm nằm ở NIC khác |
| TTL quá thấp | `set_multicast_ttl(0)` | trong máy chạy, ra ngoài thì không | `tcpdump -i <nic> host 239.10.0.1` ở đầu kia rỗng |
| Sai nhóm hoặc sai port | `--group 239.10.0.2` | im lặng | `tcpdump` thấy traffic, socket thì không |
| Traffic lạ trên cùng nhóm | gửi byte bất kỳ tới port đó | một dòng warn, không crash | `tunnel_decode` loại bằng magic — đây là lý do magic tồn tại |
| Phân mảnh | gửi > 1472 byte | `send_to` từ chối | log `datagram would fragment` |
| Sequence wrap | `SequenceTracker` từ `0xFFFFFFFE` | với `<` ngây thơ: im lặng vĩnh viễn | case `SurvivesTheThirtyTwoBitWrap` |

Quy trình truy vết (CLAUDE.md §9) cho *"cluster không hiện gì"*:

```text
gateway có gửi không?     → stdout của nó, rồi `tcpdump -i lo -n port 30490`
có rời khỏi máy không?    → tcpdump trên NIC gửi
có tới máy nhận không?    → tcpdump trên NIC nhận
có tới socket không?      → `ss -unlp | grep 30490`, `ip maddr show`
có parse được không?      → dòng warn "not a tunnel datagram"
sequence có nhận không?   → bộ đếm Stale/Duplicate trong phần tổng kết
có giải mã được không?    → DBC có id đó không
```

**Câu hỏi đầu tiên không bao giờ là "UI hỏng à?"**

---

## 7. Câu hỏi phỏng vấn

1. Vì sao xe thêm Ethernet, và vì sao CAN không biến mất?
2. Cáp văn phòng dùng 4 đôi dây. Vì sao 100BASE-T1 chỉ dùng 1, và điều đó ép
   buộc gì về topology?
3. Kể 4 bảo đảm CAN cho mà UDP không cho. Mỗi cái được mua lại ở đâu?
4. Tín hiệu tốc độ phát 10 Hz. Biện hộ cho UDP thay vì TCP, rồi nói ứng dụng
   phải tự thêm gì để bù lại.
5. Head-of-line blocking là gì, và vì sao nó tệ hơn cả mất gói đối với dữ liệu
   chu kỳ?
6. DSCP khác arbitration của CAN chỗ nào? Cái nào được cưỡng chế, bằng gì?
7. Vì sao `SO_REUSEADDR` là bắt buộc với subscriber multicast?
8. Subscriber không nhận được gì, không có lỗi nào ở đâu cả. Kể 5 nguyên nhân
   và câu lệnh phân biệt từng cái.
9. Vì sao `send_to` từ chối payload 1500 byte thay vì để IP phân mảnh?
10. Viết phép so sánh sequence an toàn với wrap, và giải thích hỏng gì nếu
    không có.
11. Vì sao gói stale bị vứt chứ không được sắp lại đúng chỗ? Khi nào thì ngược
    lại mới đúng?
12. Vì sao header tunnel cần magic number?

---

## 8. Checklist tự kiểm

- [ ] Kể được 4 bảo đảm bị mất, không nhìn giấy.
- [ ] Giải thích được 1 đôi dây của 100BASE-T1 bằng trọng lượng bó dây và
      topology điểm–điểm.
- [ ] Biện hộ được UDP thay TCP bằng lập luận head-of-line, kèm ví dụ 10 Hz cụ
      thể.
- [ ] Tự suy ra được con số 1472 và nói được vì sao phân mảnh tệ hơn từ chối.
- [ ] Giải thích được `IP_ADD_MEMBERSHIP` đẩy cái gì ra dây, và vì sao tham số
      interface quan trọng.
- [ ] Viết lại được phép so sánh sequence an toàn với wrap từ trí nhớ.
- [ ] Kể được 5 lỗi im lặng và câu lệnh phơi bày từng cái.
- [ ] **Đã tự chạy demo cả hai chế độ và nhìn thấy tốc độ tụt ngược.**

---

**Còn nợ sau tuần 7:** VLAN (phân vùng, 3 bit PCP), TSN (đặt trước băng thông,
hàng đợi theo lịch thời gian) và PTP (IEEE 1588, đồng bộ đồng hồ — cần cho cả
timestamp khi gộp dữ liệu cảm biến lẫn cho việc lên lịch của TSN) được nêu tên
ở đây nhưng chưa cài đặt — chúng là cấu hình switch và NIC, không phải code ứng
dụng. Biết mỗi cái giải quyết vấn đề gì là đủ ở mức phỏng vấn hỏi.
