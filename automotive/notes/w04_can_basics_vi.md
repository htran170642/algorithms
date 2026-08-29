# Tuần 4 — Nền tảng CAN cho người mới hoàn toàn

> **Đọc file này TRƯỚC [w04_can_vi.md](w04_can_vi.md).**
>
> `w04_can_vi.md` viết cho người đã biết CAN. File này dựng lại lý thuyết từ số
> 0, không giả định gì. Thuật ngữ kỹ thuật giữ nguyên tiếng Anh, vì đó là từ bạn
> sẽ gặp trong datasheet và trong phòng phỏng vấn.
>
> Thời lượng: khoảng **4–5 giờ**, chia 5 buổi (xem cuối file).

---

## Bản đồ: 8 khối lý thuyết

```text
1. Vì sao có CAN            <- động cơ, không có kỹ thuật
2. Lớp vật lý               <- dominant/recessive từ đâu ra
3. Message-oriented         <- chỗ dân IT hiểu sai nhiều nhất
4. Cấu trúc frame           <- để biết arbitration nằm ở đâu
5. ARBITRATION              <- TRỌNG TÂM 1
6. 5 cơ chế phát hiện lỗi   <- nền cho khối 7
7. FAULT CONFINEMENT        <- TRỌNG TÂM 2
8. Bit timing (khái niệm)   <- chỉ cần hiểu 1 điều
```

Khối 5 và 7 là thứ code tuần 4 cài đặt. Sáu khối kia là để hai khối đó **có
nghĩa**.

---

## 1. Vì sao có CAN

Xe những năm 1980: **mỗi tín hiệu một sợi dây riêng**. Cảm biến tốc độ → đồng
hồ: một dây. Công tắc cửa → đèn báo: một dây. Một chiếc xe cao cấp có tới **2 km
dây điện**, nặng hơn 50 kg.

Bosch, năm 1986, đề xuất: **một cặp dây duy nhất, mọi thiết bị nối chung vào
đó**.

```text
TRƯỚC                          SAU (CAN)

ECU-A ─────────── ECU-B        ECU-A   ECU-B   ECU-C   ECU-D
  │  ╲          ╱  │             │       │       │       │
  │    ╲      ╱    │           ══╧═══════╧═══════╧═══════╧══
  │      ╲  ╱      │              CAN_H / CAN_L (2 dây)
ECU-C ─────────── ECU-D
   (n×(n-1)/2 dây)
```

Đó là toàn bộ động cơ. Mọi thứ còn lại là hệ quả của việc **nhiều thiết bị dùng
chung một sợi dây** — và câu hỏi hiển nhiên: *nếu hai đứa cùng nói thì sao?*
Câu trả lời cho câu hỏi đó chính là arbitration.

---

## 2. Lớp vật lý — dominant/recessive từ đâu ra

Đây là khối bạn **bắt buộc** phải nắm, vì mọi thứ khác suy ra từ nó.

### Hai dây, tín hiệu vi sai (differential)

CAN dùng hai dây xoắn vào nhau: **CAN_H** và **CAN_L**. Giá trị logic không nằm
ở điện áp của từng dây, mà ở **hiệu số** giữa chúng.

```text
RECESSIVE (logic 1)              DOMINANT (logic 0)
không ai lái, dây thả nổi        có người lái

CAN_H  ──── 2.5V ────            CAN_H  ──── 3.5V ────
                                                | 2V
CAN_L  ──── 2.5V ────            CAN_L  ──── 1.5V ────

hiệu = 0V                        hiệu = 2V
```

**Vì sao phải vi sai?** Trong xe có động cơ, bộ đánh lửa, motor — nhiễu điện từ
khủng khiếp. Nhiễu tác động lên **cả hai dây xoắn như nhau**, nên **hiệu số
không đổi**. Đây là lý do CAN sống được trong môi trường xe hơi mà một dây đơn
thì không.

### Vì sao dominant "thắng" recessive

Đây là mấu chốt. Nhìn vào mạch:

```text
        +5V
         │
        ┌┴┐  điện trở kéo
        └┬┘
CAN_H ───┼──────────────────  (dây chung, mọi node nối vào)
         │
      ┌──┴──┐  transistor
      │  V  │  của node A
      └──┬──┘
        GND
```

- **Recessive** = transistor **tắt** → không ai kéo → điện trở kéo dây về 2.5V.
- **Dominant** = transistor **bật** → kéo dây xuống.

Giờ hãy tưởng tượng 4 node cùng nối vào:

| Node A | Node B | Node C | Node D | Bus |
|---|---|---|---|---|
| 1 (thả) | 1 (thả) | 1 (thả) | 1 (thả) | **1** |
| 1 (thả) | 1 (thả) | **0 (kéo)** | 1 (thả) | **0** |
| **0** | 1 | **0** | 1 | **0** |

**Chỉ cần MỘT node kéo là cả bus bị kéo xuống.** Ba node đang "thả" không cản
được — thả thì không có lực.

Về mặt logic đây chính là phép **AND**:

```text
bus = A AND B AND C AND D
```

Nên người ta gọi là **wired-AND** (AND thực hiện bằng dây, không bằng cổng
logic).

> **Nếu chỉ nhớ một câu ở khối này:** *0 là trạng thái có lực, 1 là trạng thái
> không lực. Ai kéo thì thắng.*

### Điện trở đầu cuối 120Ω

Hai đầu bus phải có điện trở 120Ω. Không có nó, tín hiệu chạy tới đầu dây rồi
**dội ngược lại** (giống tiếng vang) và làm hỏng bit tiếp theo. Đây là lỗi phần
cứng phổ biến nhất khi lắp bus thật — và ở tuần 5 bạn sẽ không gặp nó vì `vcan`
là bus ảo, không có dây để dội.

---

## 3. Message-oriented, không phải address-oriented

**Đây là chỗ người có nền IT/mạng hiểu sai nhiều nhất.** Nếu bạn quen TCP/IP,
hãy tạm quên nó đi.

| | Ethernet / IP | CAN |
|---|---|---|
| Gói tin mang gì | **địa chỉ đích** | **nhãn nội dung** |
| Nghĩa | "gửi cho máy 192.168.1.5" | "đây là *tốc độ xe*" |
| Ai nhận | đúng một máy | **mọi node**, ai quan tâm thì giữ |
| Địa chỉ người gửi | có | **không có** |

Một CAN frame **không có** địa chỉ người gửi, **không có** địa chỉ người nhận.
Nó chỉ có một con số gọi là **identifier (ID)**, và con số đó nói *nội dung này
là gì*, chứ không nói *gửi cho ai*.

```text
ECU động cơ phát:  ID=0x100, data = [tốc độ]
                          │
              ════════════╪═══════════════════
                    │     │      │       │
                 cluster ABS  gateway  radio
                    │     │      │       │
                 "cần"  "cần"  "cần"  "kệ, bỏ"
```

Mỗi node có **acceptance filter** làm bằng **phần cứng** — controller tự loại bỏ
frame không quan tâm, CPU không hề bị đánh thức.

### Ba hệ quả phải nhớ

1. **Thêm ECU mới = chỉ cần nối dây.** Không phải cấu hình lại ai cả. Không có
   DHCP, không có bảng định tuyến.
2. **ID vừa là nhãn nội dung, vừa là mức ưu tiên.** Một con số hai công dụng —
   và bạn không được chọn tách rời. Đây là điều làm việc gán ID trở thành một
   quyết định kiến trúc nặng nề.
3. **Nhìn ID không biết nó nghĩa là gì.** `0x100` là tốc độ hay là nhiệt độ?
   Phải tra một file **DBC** — đó là tuần 6.

---

## 4. Cấu trúc frame — arbitration field nằm ở đâu

Bạn không cần thuộc lòng, nhưng cần **nhìn thấy** để biết phần nào tham gia
arbitration.

```text
 SOF │ Identifier 11 bit │RTR│IDE│r0│ DLC │ Data 0-8 byte │ CRC 15 │d│ACK│d│ EOF 7 │
  1  │<--- ưu tiên ----->│   │   │  │ 4b  │               │        │ │   │ │       │
  ^  │                       │   │        │               │            ^
  │  └───────────────────────┘   └────────┘               │            │
  │        ARBITRATION FIELD      CONTROL                 CRC         ACK
  │        (chỉ phần này tranh chấp)
  │
  └─ Start of Frame: 1 bit dominant, để mọi node đồng bộ
```

| Trường | Nghĩa |
|---|---|
| **SOF** | 1 bit dominant. Báo "bus không rảnh nữa", và đồng bộ đồng hồ mọi node. |
| **Identifier** | 11 bit. Nhãn nội dung + ưu tiên. |
| **RTR** | Remote Transmission Request. 0 = tôi **mang** dữ liệu. 1 = tôi **xin** dữ liệu. |
| **IDE** | Identifier Extension. 0 = ID 11 bit. 1 = ID 29 bit. |
| **DLC** | Data Length Code, 4 bit. Số byte dữ liệu. |
| **CRC** | 15 bit kiểm tra lỗi. |
| **ACK** | Bên phát gửi 1; **bên nhận** kéo 0 để báo "tôi nhận được". |
| **EOF** | 7 bit recessive, kết thúc frame. |

**Điều duy nhất cần rút ra:** chỉ **Identifier + RTR (+ IDE)** tham gia tranh
chấp. Từ DLC trở đi thì cuộc đua đã xong, chỉ còn một node đang nói.

---

## 5. ARBITRATION — trọng tâm thứ nhất

### Kịch bản

Bus đang rảnh. Ba ECU cùng lúc muốn nói. Chuyện gì xảy ra?

```text
Bước 1: cả ba cùng gửi SOF (dominant)  -> đồng bộ, cùng vạch xuất phát
Bước 2: cả ba cùng gửi ID, bit cao nhất trước
Bước 3: mỗi node VỪA GỬI VỪA ĐỌC LẠI dây
Bước 4: ai gửi 1 mà đọc được 0 -> "có đứa ID thấp hơn" -> im lặng ngay
```

Ví dụ cụ thể, ID: `0x100`, `0x200`, `0x080`

```text
        0x100 = 0 0 1 0 0 0 0 0 0 0 0
        0x200 = 0 1 0 0 0 0 0 0 0 0 0
        0x080 = 0 0 0 1 0 0 0 0 0 0 0
                │ │ │
bit 0:  cả ba gửi 0  -> bus 0  -> không ai thua
bit 1:  0x200 gửi 1, hai đứa kia gửi 0 -> bus 0
        -> 0x200 gửi 1 đọc 0 -> THUA, rút lui
bit 2:  0x100 gửi 1, 0x080 gửi 0 -> bus 0
        -> 0x100 THUA, rút lui

        -> 0x080 thắng, và nó CHƯA HỀ BIẾT là vừa có cuộc đua
```

### Ba tính chất phải nói được

| Tính chất | Nghĩa | Vì sao có |
|---|---|---|
| **Non-destructive** | không frame nào bị hỏng | kẻ thua rút trước khi gửi bit sai |
| **Deterministic** | tính trước được ai thắng | không có yếu tố ngẫu nhiên nào |
| **Priority-based** | ID thấp thắng | MSB-first + 0 đè 1 |

### So với Ethernet cổ điển (CSMA/CD)

```text
CAN                              Ethernet cổ điển
──────────────────────           ──────────────────────
phát hiện TRƯỚC khi hỏng         phát hiện SAU khi đã hỏng
kẻ thua rút, im lặng             CẢ HAI vứt gói đi
kẻ thắng đi tiếp, không trễ      cả hai chờ NGẪU NHIÊN rồi thử lại
tính được worst-case             KHÔNG tính được worst-case
```

**Hệ quả quan trọng nhất, và là lý do CAN tồn tại trong xe:** vì arbitration là
xác định, bạn **tính được chính xác thời gian tệ nhất** mà một message phải chờ.
Đó là nền tảng của phân tích **WCET** trong ISO 26262 (tuần 14). Một hệ thống mà
bạn không tính được worst-case thì không thể chứng minh an toàn.

**Mặt trái:** message ID thấp phát liên tục có thể làm message ID cao **chết đói
(starvation)**. Đây là lý do việc phân bổ ID là bài toán thiết kế nghiêm túc,
không phải đánh số tùy tiện.

---

## 6. Năm cơ chế phát hiện lỗi

Phải học khối này **trước** khối 7, vì error counter đếm chính những lỗi này.

| # | Cơ chế | Cách hoạt động |
|---|---|---|
| 1 | **Bit monitoring** | Node phát đọc lại từng bit. Gửi khác đọc → **bit error**. *(Trừ trong arbitration field và ACK slot — ở đó khác nhau là hợp lệ.)* |
| 2 | **Bit stuffing** | Sau **5 bit giống nhau liên tiếp**, node phát chèn 1 bit ngược lại. Bên nhận thấy 6 bit giống nhau → **stuff error**. |
| 3 | **CRC** | 15 bit. Bên nhận tính lại và so sánh → **CRC error**. |
| 4 | **Form check** | Các bit có giá trị cố định (CRC delimiter, ACK delimiter, EOF) phải recessive. Sai → **form error**. |
| 5 | **ACK check** | Bên phát gửi ACK slot **recessive**; mọi bên nhận đúng sẽ kéo **dominant**. Bên phát vẫn đọc recessive → **không ai nhận được** → **ACK error**. |

### Hai chi tiết đáng nhớ

**Bit stuffing tồn tại để làm gì?** CAN **không có dây clock riêng**. Mọi node
phải tự giữ nhịp bằng cách nhìn các lần chuyển mức tín hiệu. Nếu có 20 bit `0`
liên tiếp thì không có lần chuyển nào → đồng hồ trôi dần → đọc sai. Bit stuffing
ép phải có chuyển mức ít nhất mỗi 5 bit.

**ACK là ACK tập thể.** Bên phát chỉ biết *"có ít nhất một node nào đó nhận
được"* — nó **không biết là ai**, và không biết có bao nhiêu. Nếu bạn cần biết
đích danh ai nhận, phải xây protocol ở tầng trên (đó chính là SOME/IP ở tuần 8).

**Con số đáng nhớ:** tỉ lệ lỗi lọt qua tất cả 5 cơ chế khoảng **10^-11** — thực
tế là một lỗi không bị phát hiện trong khoảng **1000 năm** vận hành liên tục.
Đây là lý do người ta dám dùng CAN cho phanh và túi khí.

---

## 7. FAULT CONFINEMENT — trọng tâm thứ hai

### Error frame: phá hoại có chủ đích

Khi **bất kỳ** node nào phát hiện lỗi, nó lập tức gửi **error frame = 6 bit
dominant liên tiếp**.

6 bit giống nhau → **vi phạm luật bit stuffing** → **MỌI node khác cũng thấy
lỗi** → tất cả cùng vứt frame → bên phát phát lại.

```text
      frame đang truyền
   ─────────────────────X═════════════
                        │
            node C thấy lỗi, kéo 6 bit dominant
                        │
                        v
        MỌI node đều thấy -> MỌI node cùng vứt
```

**Đây là điểm tinh tế nhất của cả tuần 4.** Error frame là một hành động **cố ý
phá hỏng frame trên bus**, và mục đích là đảm bảo **tính nhất quán toàn cục**:

> Hoặc **mọi** node nhận được frame, hoặc **không** node nào nhận được.

Không có trạng thái lưng chừng "một nửa số ECU biết tốc độ mới, một nửa còn dùng
tốc độ cũ". Với một hệ thống an toàn, tình trạng lưng chừng đó còn tệ hơn là mất
hẳn dữ liệu.

### Vấn đề nảy sinh

Nếu một node **bị hỏng** và cứ liên tục tưởng có lỗi rồi gửi error frame?

**Nó sẽ giết cả bus.** Mọi frame đều bị phá. Xe mất hết tín hiệu.

→ Cần một cơ chế **tự loại bỏ node hỏng**. Đó là fault confinement.

### Ba trạng thái

```text
        TEC>127 hoặc REC>127          TEC>=256
Active ────────────────────► Passive ──────────► Bus Off
```

| Trạng thái | Được làm gì | Mất gì |
|---|---|---|
| **Error active** | mọi thứ | — |
| **Error passive** | vẫn phát, vẫn nhận | error flag đổi thành **6 bit recessive** (không ai nghe thấy) + phải chờ thêm 8 bit trước mỗi lần phát |
| **Bus off** | không gì cả | ngắt hẳn bộ phát |

**Giờ hãy hiểu vì sao error passive gửi flag bằng bit RECESSIVE:**

Đó **chính là** cách tước đi khả năng phá bus của một node đáng ngờ. Recessive =
không có lực = không phá được gì. Node vẫn có thể làm việc, nhưng lời phàn nàn
của nó bị vô hiệu hóa.

Đây là mảnh ghép làm cả hệ thống hợp lý: một node càng đáng ngờ thì tiếng nói
của nó càng bị giảm dần, chứ không phải bị cắt đột ngột.

### Hai bộ đếm, hai bất đối xứng

**TEC** (Transmit Error Counter) và **REC** (Receive Error Counter).

| Sự kiện | Thay đổi |
|---|---|
| Lỗi khi đang **phát** | TEC **+8** |
| Lỗi khi đang **nhận** | REC **+1** |
| Phát thành công | TEC **-1** |
| Nhận thành công | REC **-1** |

**Bất đối xứng 1 — lỗi trừ 8, thành công hoàn 1.** Node hỏng nhiều hơn **1 frame
trên 8** sẽ leo dần lên bus-off thay vì lơ lửng. Lỗi chập chờn — thứ khó chẩn
đoán nhất — bị **đẩy thành lỗi dứt khoát**.

**Bất đối xứng 2 — phát trừ 8, nhận trừ 1.** Khi có lỗi, node **đang nói** là
thủ phạm khả dĩ nhất. Nó bị phạt nặng gấp 8 lần những node đang nghe.

Kết quả: **một ECU hỏng sẽ tự loại bỏ chính nó**, thay vì kéo cả 20 node đang
nghe rơi vào bus-off cùng.

**Hệ quả phải nhớ:** bus-off chỉ do **TEC >= 256**. Một node **chỉ nghe** không
bao giờ rơi vào bus-off — tệ nhất nó chỉ tới error passive.

---

## 8. Bit timing — chỉ cần hiểu 1 điều ở tuần 4

**Vì sao tốc độ bit bị giới hạn bởi chiều dài bus?**

Vì trong arbitration, tín hiệu phải chạy tới **đầu xa nhất** của bus **và quay
về**, tất cả **trong vòng một bit time**, để node kia kịp so sánh cái nó gửi với
cái trên dây.

| Tốc độ | Chiều dài tối đa |
|---|---|
| 1 Mbit/s | 40 m |
| 500 kbit/s | 100 m |
| 250 kbit/s | 250 m |
| 125 kbit/s | 500 m |

Chi tiết (sync segment, propagation segment, phase segment, sample point, SJW)
để **tuần 5**, khi bạn thực sự gõ `ip link set can0 type can bitrate 500000`.

---

## Cái gì phải thuộc, cái gì chỉ cần hiểu, cái gì bỏ qua

| Mức | Nội dung |
|---|---|
| **PHẢI THUỘC** *(nói ra được ngay, không nghĩ)* | dominant=0 / recessive=1 · wired-AND · ID thấp thắng · non-destructive nghĩa là gì · 3 trạng thái error · TEC>=256 → bus-off · lỗi +8, thành công -1 |
| **PHẢI HIỂU** *(giải thích được cơ chế)* | vì sao differential chống nhiễu · vì sao message-oriented · 5 cơ chế phát hiện lỗi · error frame = 6 bit dominant và vì sao · vì sao error passive dùng recessive flag · vì sao chiều dài giới hạn tốc độ |
| **BIẾT LÀ CÓ** *(không cần chi tiết)* | thứ tự chính xác các trường trong frame · thuật toán CRC · quy tắc chi tiết của bit stuffing · cấu trúc error delimiter |
| **BỎ QUA Ở TUẦN 4** | bit timing chi tiết → tuần 5 · CAN-FD → tuần 5–6 · DBC → tuần 6 · CAN XL, LIN, FlexRay → không cần |

---

## Thứ tự học đề xuất

| Buổi | Việc | Thời gian |
|---|---|---|
| 1 | Khối 1–3 (động cơ + lớp vật lý + message-oriented). **Tự vẽ lại** mạch wired-AND ra giấy. | 45 ph |
| 2 | Khối 4–5 (frame + arbitration). **Tự làm tay** một cuộc đua 3 ID trên giấy, không nhìn đáp án. | 60 ph |
| 3 | Khối 6 (5 cơ chế phát hiện lỗi). Không cần thuộc chi tiết, cần **nói được mỗi cái bắt loại lỗi nào**. | 40 ph |
| 4 | Khối 7 (error frame → fault confinement). Đây là khối khó nhất về mặt *ý tưởng*, không phải kỹ thuật. | 60 ph |
| 5 | Khối 8 (30 phút) + đọc [w04_can_vi.md](w04_can_vi.md) §1–§3. | 45 ph |
| 6 | **Rồi mới vào code.** | — |

---

## 10 câu tự kiểm — trả lời được hết rồi hãy vào code

Viết ra giấy, không nhìn tài liệu:

1. Vì sao một node kéo dominant thì cả bus thành dominant, dù ba node khác đang
   gửi recessive?
2. Vì sao CAN dùng hai dây thay vì một?
3. CAN frame gửi cho ai? *(Câu hỏi bẫy.)*
4. ID `0x100` và `0x0FF` cùng phát — cái nào thắng, và ở **bit thứ mấy**?
5. "Non-destructive" nghĩa chính xác là gì? Kẻ thua mất gì?
6. Kể 3 trong 5 cơ chế phát hiện lỗi và mỗi cái bắt loại lỗi nào.
7. Vì sao error frame là **6 bit dominant** chứ không phải một mã đặc biệt?
8. Một node error passive **vẫn làm được gì** và **mất gì**?
9. Một node chỉ nghe, không bao giờ phát — nó có thể rơi vào bus-off không? Vì
   sao?
10. Vì sao lỗi trừ 8 mà thành công chỉ hoàn 1?

Câu **3, 5, 7, 9** là bốn câu phân biệt người hiểu với người thuộc lòng.

---

## Rồi vào code theo thứ tự này

```text
1. libs/av_can/include/av/can/arbitration.hpp   <- đọc COMMENT trước, code sau
2. libs/av_can/src/arbitration.cpp              <- hàm arbitration_field()
3.                                              <- rồi hàm arbitrate()
4. ./build/debug/apps/can_bus/can_bus           <- CHẠY, xem bảng
5. libs/av_can/tests/arbitration_test.cpp       <- test = đặc tả hành vi
6. error_state.hpp -> .cpp -> test              <- khối 7
```

Bước 4 nên làm **ngay sau bước 3**, đừng để cuối. Nhìn thấy ba node rớt trong ba
bit là lúc lý thuyết "khớp" vào chỗ.

---

## Nguồn tra cứu khi cần đào sâu

| Nguồn | Dùng cho |
|---|---|
| **Bosch CAN Specification 2.0** (1991) | tài liệu gốc; phần B mô tả extended frame |
| **ISO 11898-1** | data link layer, chính thức hóa Bosch 2.0 |
| **ISO 11898-2** | high-speed physical layer — điện áp, 120Ω, chiều dài bus |
| `man 7 can` trên Linux | SocketCAN, chuẩn bị cho tuần 5 |

Không cần đọc hết. Chúng là để tra khi có câu hỏi cụ thể, không phải để học
tuần tự.
