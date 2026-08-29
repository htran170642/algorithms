# Tuần 4 — Nền tảng CAN: arbitration và fault confinement

> Bản tiếng Việt của [w04_can.md](w04_can.md). Thuật ngữ kỹ thuật giữ nguyên
> tiếng Anh, vì đó là từ bạn sẽ gặp trong datasheet, trong tài liệu ISO, và
> trong phòng phỏng vấn.
>
> **Chưa từng học CAN?** Đọc [w04_can_basics_vi.md](w04_can_basics_vi.md) trước
> — nó dựng lý thuyết từ số 0. File này giả định bạn đã biết bus là gì,
> dominant/recessive là gì, và một CAN frame gồm những trường nào.

## 1. Khái niệm

Tuần 1–3 đều sống *phía trên* driver: một payload, một queue, một socket. Tuần
này đi xuống dưới nó, tới hai cơ chế mà CAN controller chạy bằng phần cứng và
**không bao giờ xuất hiện trong một lần `read()`**:

| | trả lời câu hỏi gì | tốn gì của một node |
|---|---|---|
| **Arbitration** | ai được chiếm dây khi nhiều ECU cùng bắt đầu | không gì cả — kẻ thua rút lui nguyên vẹn |
| **Fault confinement** | một ECU hỏng bị loại bỏ thế nào | error passive, rồi bus-off |

Hai sự thật vật lý sinh ra toàn bộ phần arbitration:

1. **Bus là wired-AND.** Bit dominant (0) chủ động kéo dây xuống; bit recessive
   (1) chỉ thả cho nó nổi. Nếu **bất kỳ** node nào kéo dominant, **mọi** node
   đều đọc được dominant.
2. **Mọi node phát đều đọc lại cái nó vừa gửi.** Node nào gửi recessive mà đọc
   được dominant thì biết có người ID thấp hơn đang nói, dừng ngay lập tức, và
   trở thành node nhận.

Kết hợp lại, đó là lý do arbitration **non-destructive**: kẻ thua biết được
*trước khi* nó đặt một bit sai nào lên dây, nên frame của kẻ thắng không bị
hỏng, không bị trễ, và không phải phát lại. Ethernet phát hiện collision **sau
khi** nó đã xảy ra rồi vứt cả hai frame đi. CAN giải quyết collision mà không
bao giờ để nó xảy ra.

Quy tắc ưu tiên là hệ quả miễn phí. Identifier được phát ra **bit cao nhất
trước (MSB-first)**, và 0 đè 1, nên ID nhỏ hơn về mặt số học sẽ thắng. Đó không
phải một quy ước ai đó chọn — nó rơi ra từ cách đấu dây.

### Hai tiêu chí phân định khi hòa

Cùng ID, khác loại frame — cuộc đua tiếp tục qua khỏi phần identifier:

| Vị trí 11 | 12 | Kết quả |
|---|---|---|
| data frame kéo **RTR = 0**, remote kéo **RTR = 1** | — | data thắng remote: cung cấp giá trị hơn đi hỏi giá trị |
| standard kéo **RTR**, extended bắt buộc kéo **SRR = 1** (luôn luôn) | standard **IDE = 0**, extended **IDE = 1** | standard thắng extended cùng base ID — ở bit 11 nếu standard mang data, ngược lại ở bit 12 |

Vị trí 11 mới là chỗ thú vị. Nó là RTR với một standard frame và SRR với một
extended frame — **cùng một vị trí trên dây**, hai cái tên — và extended frame
*bắt buộc* phải kéo nó recessive. Nó không thể thắng ở đó.

### Fault confinement

Ba trạng thái, điều khiển bởi hai bộ đếm (TEC cho phát, REC cho nhận):

```text
        TEC>127 hoặc REC>127          TEC>=256
Active ────────────────────► Passive ──────────► Bus Off
   ▲                            │                   │
   └────────────────────────────┘                   │
      bộ đếm tụt xuống lại        128 cửa sổ        │
                                  11 bit recessive ─┘
```

- **Error active** — bình thường. Báo lỗi bằng 6 bit **dominant**, tức là **cố
  ý phá hỏng** frame trên bus để mọi node cùng vứt nó đi.
- **Error passive** — vẫn phát và vẫn nhận. Nhưng error flag của nó là 6 bit
  **recessive**, tức là không ai để ý, và nó phải chờ thêm 8 bit recessive
  trước khi bắt đầu phát, nên nó **luôn thua** cuộc đua giành một bus rảnh.
- **Bus off** — bộ phát bị ngắt. Node giờ vắng mặt về mặt điện; bus không hề
  suy giảm chút nào.

Hai bất đối xứng gánh toàn bộ thiết kế:

- **Một lỗi trừ 8, một lần thành công hoàn 1.** Node nào hỏng nhiều hơn một
  frame trên tám sẽ leo dần lên bus-off thay vì lơ lửng. Lỗi chập chờn bị **đẩy
  lên**, không được dung thứ.
- **Node phát bị trừ 8, node nhận chỉ 1.** Khi có lỗi xuất hiện, node đang nói
  là thủ phạm khả dĩ nhất. Đó là lý do một ECU với transceiver hỏng **tự loại
  bỏ mình** thay vì kéo mọi node đang nghe đi theo.

Hệ quả đáng nói thành lời: **REC một mình không bao giờ gây bus-off.** Một node
chỉ nghe thì tệ nhất cũng chỉ tới error passive, bus có tệ đến đâu đi nữa.

## 2. Vì sao nó tồn tại trong Cockpit DC

Cluster dùng chung bus với powertrain, phanh và body. Ba điều suy ra trực tiếp
từ tuần này:

- **Ưu tiên là một quyết định gán ID, làm một lần, ở thời điểm thiết kế.** Đặt
  message tốc độ của cluster một ID cao thì nó thua mọi cuộc đua trước tiếng ồn
  của một body controller. **Không có núm vặn lúc runtime nào sửa được chuyện
  đó.** Cùng lý lẽ với thread priority — do kiến trúc sư chọn, do phần cứng thi
  hành.
- **Một cockpit phải sống sót qua một hàng xóm hỏng.** Fault confinement là lý
  do một ECU hỏng chỉ là *một signal biến mất*, không phải một cái bus chết. Nó
  tương đương ở tầng CAN với isolation address space của tuần 3 và với freedom
  from interference của hypervisor ở tuần 13 — cùng một nguyên lý ở ba quy mô.
- **Bộ đếm chính là bằng chứng.** Khi một signal biến mất, TEC/REC trên
  controller phát phân biệt "ECU đã rời bus" với "ECU vẫn khỏe và chẳng ai hỏi
  nó giá trị". CLAUDE.md §9 nói đừng vội cho là UI hỏng; tuần này cấp cho bạn
  hai dòng dưới cùng của chuỗi truy vết đó.

## 3. Kiến trúc

```text
        Cockpit application               <- tuần 8-13
                 |
             Middleware                   <- tuần 8-10
                 |
             SocketCAN                    <- tuần 5
                 |
             CAN driver
                 |
   +-------------+-------------------+
   |      CAN controller             |    <- TUẦN NÀY
   |                                 |
   |   arbitration    error counters |
   |   (ai được nói)  (ai được ở lại)|
   +-------------+-------------------+
                 |
          CAN transceiver
                 |
         CAN_H / CAN_L (wired-AND)
```

Mọi thứ trong vùng đóng khung là phần cứng. Phần mềm không bao giờ nhìn thấy nó
— và chính vì thế các lỗi của nó tới được tầng ứng dụng dưới lốt một thứ khác.

## 4. Cài đặt C++ / Linux

| File | Vai trò |
|---|---|
| [arbitration.hpp](../libs/av_can/include/av/can/arbitration.hpp) | `Contender`, `BitValue`, `ArbitrationResult`; layout của arbitration field ghi ở đúng một chỗ |
| [arbitration.cpp](../libs/av_can/src/arbitration.cpp) | dựng arbitration field của từng node, rồi duyệt từng bit áp dụng wired-AND |
| [error_state.hpp](../libs/av_can/include/av/can/error_state.hpp) | `ErrorCounters`, `ErrorEvent`, `ErrorState`, và ba ngưỡng |
| [error_state.cpp](../libs/av_can/src/error_state.cpp) | các quy tắc bộ đếm, gồm cả hai trường hợp đặc biệt bên dưới |
| [can_bus/main.cpp](../apps/can_bus/main.cpp) | demo: sáu kịch bản, bốn trong đó là lỗi |

Quyết định thiết kế đáng bảo vệ:

- **`Contender` không phải `CanFrame`.** Arbitration chỉ nhìn thấy ID, format và
  RTR — mà `CanFrame` không mang RTR, vì SocketCAN báo nó out-of-band. Mô hình
  hóa đúng những gì arbitration field chứa giữ cho mô phỏng không lén phụ thuộc
  vào các trường mà bus không bao giờ thấy.
- **Một bảng tên bit duy nhất, theo cách đánh số 29-bit.** Standard frame gọi vị
  trí 0–10 là `ID10..ID0`, extended frame gọi đúng những vị trí đó là
  `ID28..ID18`. **Chúng là cùng những bit.** Vị trí 11 thì thực sự có hai công
  dụng, nên được đặt tên `RTR/SRR` — cái tên trung thực duy nhất.
- **Kết quả mang một `winners` dạng *vector*, không phải một chỉ số.** Hai ECU
  cùng ID thì **cả hai** đều sống sót qua arbitration. Trả về một winner duy
  nhất sẽ giấu một configuration fault thật sau một câu trả lời nghe hợp lý.
- **Bus-off là latch.** TEC tụt xuống dưới 256 **không** đưa node lên bus trở
  lại; chỉ chuỗi 128 cửa sổ khôi phục mới làm được. Mô hình hóa nó bằng một
  `bool` thay vì một phép so ngưỡng là thứ khiến test `BusOffFreezesTheCounters`
  diễn đạt được.
- **REC bão hòa ở 256.** Nó không bao giờ gây bus-off, nhưng bộ đếm của một
  controller thật chỉ rộng vài bit. Cho bão hòa ngăn một lỗi kéo dài quấn vòng
  về một giá trị trông có vẻ khỏe mạnh.

## 5. Bài thực hành

```bash
./check.sh                              # debug + asan + ubsan + tsan + clang-tidy
./build/debug/apps/can_bus/can_bus      # sáu kịch bản
```

Bảng đầu tiên là thứ đáng nhìn chằm chằm:

```text
  bit  name      BRAKE    ENGINE   BODY     INFO     bus
  0    ID28      0        0        0        1        0   <- INFO gửi 1, đọc 0, rút lui
  1    ID27      0        0        1        .        0   <- BODY gửi 1, đọc 0, rút lui
  2    ID26      0        1        .        .        0   <- ENGINE gửi 1, đọc 0, rút lui
  bus granted winner=BRAKE bits=3
```

Ba ECU bị loại trong ba bit: không collision, không retry, không backoff.

## 6. Bài tập lỗi / debug

| Lỗi tiêm vào | Phát hiện bởi | Log ra | Fallback | Safe state |
|---|---|---|---|---|
| Hai ECU trùng ID | **không ai cả** — arbitration hoàn tất | `arbitration cannot resolve this still_transmitting=2` | không có gì ở tầng này | mức bus: cả hai frame bị phá trong data field, cả hai retry, error frame lác đác |
| Transceiver hỏng khi phát | TEC của controller | `state change failed_frames=16 tec=128 state=passive` | node vẫn phát, nhưng không báo lỗi được | error passive |
| Lỗi đó tiếp diễn | TEC của controller | `state change failed_frames=32 tec=256 state=bus-off` | node ngắt bộ phát của nó | bus-off; bus không ảnh hưởng |
| Bus im lặng | 128 cửa sổ 11 bit recessive | `recovered after a quiet bus windows=128 state=active` | xóa bộ đếm | error active |

### Silent fault của tuần 4

**Một node rơi vào bus-off và bus trở nên *tốt hơn*.**

Kịch bản 6 của demo cho thấy điều đó. BRAKE biến mất; ENGINE, BODY và INFO
arbitrate hoàn hảo. Tải thấp hơn. Latency cải thiện. Mọi signal còn lại decode
được. `candump` cho ra một cái bus sạch bong, khỏe mạnh.

Cluster thấy đúng một giá trị ngừng cập nhật — và mọi bản năng đều chỉ vào sai
tầng:

| Tầng | Nó báo gì |
|---|---|
| QML | binding ổn, giá trị cũ |
| Qt model | không nhận được update |
| Middleware | không publish event nào |
| DBC decoder | không được gọi |
| SocketCAN | không có frame với ID đó — **và cũng không có lỗi nào** |
| Controller | TEC = 256, bus-off ← **bằng chứng thật** |

Không có gì trong stack phần mềm sai cả. Bằng chứng nằm **dưới driver một
tầng**, trong một bộ đếm mà không ứng dụng nào đọc. Đây là lý do "signal biến
mất" và "bus hỏng" là hai chẩn đoán khác nhau, và là lý do câu hỏi đầu tiên
phải là *dữ liệu còn đúng ở đâu lần cuối* (CLAUDE.md §9).

Bảng theo dõi các silent fault:

| Tuần | Lỗi | Vì sao không gì bắt được |
|---|---|---|
| 1 | sai byte order | một con số sai nhưng hợp lý; CRC và ACK đều pass |
| 2 | thiếu `close()` | không output gì cả; `join()` treo mãi mãi |
| 3 | thiếu `release` store | đúng trên x86, sai trên ARM |
| 4 | node rơi vào bus-off | bus trông **khỏe hơn** trước |

## 7. Câu hỏi phỏng vấn cấp Senior

**1. Vì sao CAN arbitration là non-destructive, còn Ethernet cần backoff?**

Vì một node phát trên CAN đọc lại từng bit nó gửi trên một bus wired-AND. Node
gửi recessive mà đọc dominant thì biết mình đã thua *ngay tại bit đó* và dừng
trước khi đóng góp bất cứ thứ gì sai. Frame của kẻ thắng nguyên vẹn. Collision
của Ethernet chỉ được phát hiện sau khi cả hai trạm đã làm hỏng môi trường
truyền, nên cả hai phải vứt đi và thử lại sau một khoảng trễ ngẫu nhiên. CAN trả
giá cho điều này bằng một bit time đủ dài để tín hiệu chạy tới đầu kia của bus
và quay về — đó là lý do tốc độ bit của CAN bị giới hạn bởi chiều dài bus.

**2. Vì sao ID nhỏ hơn có ưu tiên cao hơn?**

Identifier được phát bit cao nhất trước, và 0 (dominant) đè 1 (recessive). Ở bit
đầu tiên mà hai ID khác nhau, cái nào có 0 sẽ thắng. "Giá trị nhỏ hơn thắng"
chỉ là hệ quả số học của việc phát MSB-first trên một bus wired-AND.

**3. Standard frame và extended frame cùng base ID — cái nào thắng?**

Standard frame, luôn luôn. Ở vị trí 11 trên dây, standard frame kéo RTR; với một
data frame đó là dominant, và extended frame — vốn bắt buộc phải kéo SRR
recessive ở đó — thua. Nếu standard frame lại là remote frame, cả hai đều kéo
recessive và vị trí 12 (IDE) quyết định: dominant với standard, recessive với
extended. Cùng kết quả, chậm hơn một bit.

**4. Chuyện gì thực sự xảy ra khi hai ECU bị cấu hình trùng ID?**

Arbitration không giải quyết được. Cả hai kéo những field giống hệt nhau, cả hai
tin mình đã thắng, và chúng phân kỳ trong data field — nơi sự lệch nhau là một
bit error, không phải một cú thua arbitration. Error frame, retry, và TEC leo
trên cả hai node. Triệu chứng là hỏng dữ liệu lác đác, **không bao giờ** là một
lời phàn nàn về ưu tiên, và vì thế nó trông giống lỗi đi dây.

**5. Một node chỉ nghe có bao giờ rơi vào bus-off được không?**

Không. Bus-off là TEC ≥ 256, mà một node nhận chỉ làm dịch chuyển REC. Tệ nhất
nó chạm tới error passive. Điều này là cố ý: node có khả năng có lỗi nhất là
node đang phát, nên nó bị trừ 8 mỗi lỗi trong khi những node đang nghe chỉ bị
trừ 1.

**6. Khác biệt thực tế giữa error active và error passive là gì?**

Gần như không có gì mà tầng ứng dụng thấy được — và đó chính là chỗ nguy hiểm.
Một node error passive vẫn phát và vẫn nhận. Nó mất hai thứ: error flag của nó
là recessive nên không ai để ý tới lời phàn nàn của nó, và nó phải chờ thêm 8
bit recessive trước khi bắt đầu phát, nên nó thua mọi cuộc đua giành một bus
rảnh. Triệu chứng là **latency chập chờn và hỏng dữ liệu không được báo cáo** —
không phải một cú mất kết nối.

**7. Một signal ngừng xuất hiện trên cluster. Bạn nhìn vào đâu trước?**

Không phải UI. Xác định xem dữ liệu còn đúng ở đâu lần cuối. Nếu `candump` không
cho thấy frame nào với ID đó, vấn đề nằm ở hoặc dưới driver: kiểm tra error
state của controller đang phát. Bus-off là câu trả lời sạch sẽ; error passive
trên một bus bận là câu trả lời khó chịu hơn. Nếu frame **có** trên bus thì lỗi
nằm trên driver và cuộc tìm kiếm chuyển sang dòng DBC, decoder, và model
binding.

**8. Vì sao một lỗi trừ 8 mà một lần thành công chỉ hoàn 1?**

Để bộ đếm hoạt động như một cái bánh cóc (ratchet). Ở tỉ lệ 1:1, một node hỏng
một nửa số frame sẽ ngồi lì quanh 0 mãi mãi. Ở tỉ lệ 8:1, bất cứ thứ gì tệ hơn
một lỗi trên tám sẽ leo đều đặn tới bus-off. Sự bất đối xứng biến "hỏng chập
chờn" — trạng thái khó chẩn đoán nhất — thành "bị loại bỏ dứt khoát".

## 8. Checklist tự kiểm

- [x] Giải thích được mà không cần nhìn note
- [x] Tự cài đặt, không phải chỉ đọc
- [x] Cố ý làm hỏng và xem nó hỏng (4 lỗi, §6)
- [x] Bảo vệ được đánh đổi thiết kế đã chọn (§4)
- [x] Test pass dưới `./check.sh` — debug, asan, ubsan, tsan, clang-tidy sạch

Cố ý để lại:

- Bit stuffing, CRC, ACK slot, và bản thân định dạng error frame. Controller lo
  hết và chúng không bao giờ tới được phần mềm; chúng thuộc về một cuốn
  datasheet, không thuộc về codebase của một cockpit.
- Bit timing (sync/prop/phase segment, sample point, SJW). Nó quan trọng khi
  đưa một bus thật vào vận hành và sẽ quay lại ở tuần 5 với
  `ip link set can0 ...`, nơi các con số thực sự cấu hình được.
