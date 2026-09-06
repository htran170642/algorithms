# Tuần 6 — Đọc hiểu file `cockpit.dbc`, từng dòng

Note này giải thích **nội dung file DBC**. Phần thiết kế parser và các quyết định
kỹ thuật nằm ở [`w06_dbc.md`](w06_dbc.md) (tiếng Anh).

Nguồn: [`dbc/cockpit.dbc`](../dbc/cockpit.dbc) — 37 dòng, 2 message, 7 signal.

---

## 1. Vì sao cần file này

Một frame CAN **không tự mô tả chính nó**. Tất cả những gì đi trên dây là một con
số và tám byte:

```text
  vcan0  100   [8]  CC 1C 42 26 80 77 00 00
```

Không có gì trong dòng đó nói rằng `CC 1C` là tốc độ, rằng nó là little-endian,
rằng phải nhân 0.01, hay kết quả có đơn vị km/h. **File DBC là thứ duy nhất nói
điều đó** — và mọi ECU trên bus được kỳ vọng giữ cùng một bản sao.

Đây là hợp đồng **giữa các tổ chức**, không chỉ giữa hai chương trình. Đội cockpit
không được tự đặt bố cục signal; đội đó **nhận một file**.

---

## 2. Bố cục tổng thể — 5 khu vực

```text
dòng 1      VERSION      ┐
dòng 4-13   NS_ BS_ BU_  ┘ phần đầu — parser của ta chỉ đọc VERSION
dòng 16-20  BO_ + SG_    ┐
dòng 22-25  BO_ + SG_    ┘ THÂN — bố cục bit, phần duy nhất bắt buộc
dòng 29-33  CM_          → chú thích, không ảnh hưởng byte nào
dòng 35-36  VAL_         → bảng liệt kê, đổi số thành tên
```

---

## 3. Phần đầu (dòng 1–13)

```text
1   VERSION "cockpit-dc-lab 0.1"
```

Chuỗi tự do. Parser đọc ở [dbc.cpp:363-372](../libs/av_can/src/dbc.cpp#L363), lưu
vào `version_`, in ra khi load:

```text
INFO  dbc  database loaded  version=cockpit-dc-lab 0.1  messages=2
```

Không có ý nghĩa kỹ thuật — nhưng trong thực tế đây là thứ **đọc đầu tiên khi số
liệu sai**, để biết hai bên có cùng revision không.

```text
4   NS_ :
5       CM_
6       BA_DEF_
    ...
```

**New Symbols** — danh sách từ khoá mà file *có thể* dùng. Công cụ sinh ra nó,
không ai đọc. Parser bỏ qua ở [dbc.cpp:396](../libs/av_can/src/dbc.cpp#L396).

```text
11  BS_:
```

**Bit timing** — về lý thuyết chứa baudrate. Trên thực tế **luôn rỗng**; tốc độ
bus được cấu hình bằng `ip link`, không phải ở đây.

```text
13  BU_ ECU_Powertrain ECU_Body Cluster
```

**Bus Units** — danh sách node trên bus. Parser của ta bỏ qua, nhưng công cụ thật
(Vector CANdb++) dùng nó để **kiểm tra chéo**: tên sau `BO_` (bên gửi) và tên cuối
`SG_` (bên nhận) phải có mặt ở đây. Gõ sai `ECU_Powertain` → CANdb++ báo lỗi,
parser của ta thì không.

---

## 4. Dòng `BO_` — định nghĩa message

```text
BO_  256   EngineData:   8   ECU_Powertrain
 │    │        │         │        │
 │    │        │         │        └─ bên gửi (phải có trong BU_)
 │    │        │         └────────── DLC: số byte payload
 │    │        └──────────────────── tên, DẤU HAI CHẤM DÍNH LIỀN
 │    └───────────────────────────── CAN ID, hệ THẬP PHÂN
 └────────────────────────────────── từ khoá
```

Ba cái bẫy trong đúng một dòng:

| Bẫy | Vì sao nguy hiểm | Code xử lý |
|---|---|---|
| ID viết **thập phân**, không phải hex | `256` = `0x100`. Đọc nhầm thành `0x256` → không khớp frame nào, bus trông như im lặng | — |
| Dấu `:` **dính vào tên** | `>>` đọc ra `"EngineData:"`, phải cắt bỏ | [dbc.cpp:70-71](../libs/av_can/src/dbc.cpp#L70) |
| **Bit 31 = cờ extended** | `256` có bit 31 = 0 → standard 11-bit. Nhưng `2147483939` = `0x80000123` là **extended id `0x123`**, không phải id 2.1 tỷ | [dbc.cpp:80-81](../libs/av_can/src/dbc.cpp#L80) |

Bẫy thứ ba là **lỗi parse DBC phổ biến nhất**. Một parser đọc số đó theo nghĩa
đen sẽ tạo ra một message mà không frame nào trên bus khớp được.

Hai message trong file này:

| Dòng | ID thập phân | ID hex | Tên | Bên gửi |
|---|---|---|---|---|
| 16 | `256` | `0x100` | `EngineData` | `ECU_Powertrain` |
| 22 | `512` | `0x200` | `BodyState` | `ECU_Body` |

Hai ID này chính là thứ sinh ra kernel filter ở
[can_rx/main.cpp:186](../apps/can_rx/main.cpp#L186) — không có ID nào hardcode
trong code.

---

## 5. Dòng `SG_` — chín trường

```text
 SG_ VehicleSpeed : 0|16@1+ (0.01,0) [0|655.35] "km/h" Cluster
 │        │         │  │  ││    │  │   │   │       │      │
 │        │         │  │  ││    │  │   │   │       │      └─ ⑨ bên nhận
 │        │         │  │  ││    │  │   │   │       └──────── ⑧ đơn vị
 │        │         │  │  ││    │  │   │   └──────────────── ⑦ max
 │        │         │  │  ││    │  │   └──────────────────── ⑥ min
 │        │         │  │  ││    │  └──────────────────────── ⑤ offset
 │        │         │  │  ││    └─────────────────────────── ④ factor
 │        │         │  │  │└──────────────────────────────── ③ dấu
 │        │         │  │  └───────────────────────────────── ② byte order
 │        │         │  └──────────────────────────────────── ① độ dài (bit)
 │        │         └─────────────────────────────────────── ⓪ start bit
 │        └───────────────────────────────────────────────── tên
 └────────────────────────────────────────────────────────── từ khoá
```

| # | Giá trị | Nghĩa | Parser đọc ở |
|---|---|---|---|
| ⓪ | `0` | bit bắt đầu, đếm từ 0 | [dbc.cpp:126](../libs/av_can/src/dbc.cpp#L126) |
| ① | `16` | dùng 16 bit | [dbc.cpp:126](../libs/av_can/src/dbc.cpp#L126) |
| ② | `1` | **1 = Intel (little-endian)**, `0` = Motorola | [dbc.cpp:163](../libs/av_can/src/dbc.cpp#L163) |
| ③ | `+` | unsigned; `-` = two's complement | [dbc.cpp:164](../libs/av_can/src/dbc.cpp#L164) |
| ④ | `0.01` | nhân — chính là **độ phân giải** | [dbc.cpp:165](../libs/av_can/src/dbc.cpp#L165) |
| ⑤ | `0` | cộng sau khi nhân | [dbc.cpp:166](../libs/av_can/src/dbc.cpp#L166) |
| ⑥⑦ | `0` / `655.35` | khoảng hợp lệ | [dbc.cpp:167-168](../libs/av_can/src/dbc.cpp#L167) |
| ⑧ | `"km/h"` | chỉ để hiển thị | [dbc.cpp:154](../libs/av_can/src/dbc.cpp#L154) |
| ⑨ | `Cluster` | **không ai đọc** — [dbc.cpp:157](../libs/av_can/src/dbc.cpp#L157) ghi rõ | — |

Công thức, cả hai chiều:

```text
vật lý  =  raw × factor + offset                 decode, signal.cpp:33
raw     =  round((vật lý − offset) / factor)     encode, signal.cpp:44
```

**Chú ý dấu cách đầu dòng.** Dòng `SG_` bắt đầu bằng một space.
[dbc.cpp:358](../libs/av_can/src/dbc.cpp#L358) gọi `trim()` trước, nếu không thì
`starts_with(line, "SG_ ")` ở [dbc.cpp:381](../libs/av_can/src/dbc.cpp#L381) sẽ
trượt và cả file bị hiểu là "không có signal nào".

---

## 6. Bản đồ byte

### `EngineData` (0x100), 8 byte

```text
byte     0     1     2     3     4     5     6     7
bit    0-7  8-15 16-23 24-31 32-39 40-47 48-55 56-63
     ┌─────┴─────┬─────┴─────┬─────┬─────┬─────┬─────┐
     │VehicleSpeed│ EngineRpm │Cool.│Fuel │  ─  │  ─  │
     │  0|16@1+   │ 16|16@1+  │32|8 │40|8 │trống│trống│
     └───────────┴───────────┴─────┴─────┴─────┴─────┘
```

### `BodyState` (0x200), 8 byte

```text
byte     0     1     2           3     4  …  7
     ┌─────┴─────┬────┬─────┬─────┬───────────┐
     │SteeringAngle│Door│  ─  │Warn.│  trống    │
     │  0|16@1-   │16|4│20-23│24|8 │           │
     └───────────┴────┴─────┴─────┴───────────┘
                       ▲ 4 bit bỏ trống
```

`DoorStatus` chiếm bit 16–19 = **nửa thấp của byte 2**. Bit 20–23 chưa dùng — chỗ
để dành, và cũng là **lỗ hổng**: hôm nay không có gì ngăn ai đó thêm
`SG_ Foo : 18|4@1+` đè lên `DoorStatus`. Parser không phát hiện chồng lấn; đây là
thiếu sót đã ghi nhận trong [`w06_dbc.md`](w06_dbc.md).

---

## 7. Kiểm chứng bằng một frame thật

Frame `EngineData` tại tick 7 của `sim_vehicle`:

```text
  vcan0  100   [8]  CC 1C 42 26 80 77 00 00
                    └──┬──┘└──┬──┘ └┬┘ └┬┘ └──┬──┘
```

| Byte | Hex | Signal | Phép tính | Kết quả |
|---|---|---|---|---|
| 0–1 | `CC 1C` | VehicleSpeed | `0x1CCC` = 7372 × 0.01 | **73.72 km/h** |
| 2–3 | `42 26` | EngineRpm | `0x2642` = 9794 × 0.25 | **2448.5 rpm** |
| 4 | `80` | CoolantTemp | 128 × 1 + (−40) | **88 °C** |
| 5 | `77` | FuelLevel | 119 × 0.5 | **59.5 %** |
| 6–7 | `00 00` | — | không signal nào phủ | luôn 0 |

Hai byte đầu là `CC 1C` chứ **không phải** `1C CC` — đó là `@1` (Intel), byte thấp
đứng trước. Đổi thành `@0` thì cùng đống byte đó ra `0xCC1C` = 52252 →
**522.52 km/h**. Số này trông hoàn toàn hợp lệ với một chương trình, và đó chính
là silent fault của tuần 1.

Đối chiếu với `read_sensors(7)` ở
[sim_vehicle/main.cpp:58](../apps/sim_vehicle/main.cpp#L58): giá trị gốc là
`73.715912`, sau khi làm tròn về raw 7372 rồi giải mã lại thành `73.72`. Sai lệch
`0.004` là **lượng tử hoá**, không phải lỗi — nó luôn nhỏ hơn `factor / 2`.

---

## 8. `CM_` — chú thích (dòng 29–33)

```text
CM_ BO_ 256 "Powertrain cyclic data, 100 ms";
CM_ SG_ 256 VehicleSpeed "Filtered road speed, not wheel speed";
     │    │       │
     │    │       └─ tên signal (chỉ có với SG_)
     │    └───────── ID của message chứa nó
     └────────────── BO_ = chú thích message, SG_ = chú thích signal
```

Không ảnh hưởng byte nào. Nhưng nội dung đáng đọc kỹ:

- **`"cyclic data, 100 ms"`** — chu kỳ gửi nằm trong **chú thích**, không phải
  trong định dạng. Đây là điểm yếu thật của DBC: nó không có chỗ chính thức cho
  timing. Middleware muốn timeout đúng thì phải đọc `BA_` attribute hoặc một tài
  liệu riêng. Sẽ gặp lại ở tuần 10.
- **`"Filtered road speed, not wheel speed"`** — thông tin **không thể suy ra từ
  bất kỳ bit nào**. Đúng loại chú thích cứu người debug.
- **`"Offset -40 keeps the raw byte unsigned"`** — giải thích vì sao offset là
  −40: để nhiệt độ âm vẫn nằm gọn trong byte unsigned 0–255, khỏi cần two's
  complement.

Hạn chế: [dbc.cpp:178](../libs/av_can/src/dbc.cpp#L178) ghi rõ chú thích **nhiều
dòng không được xử lý** — bị bỏ qua chứ không gán nhầm cho signal khác.

---

## 9. `VAL_` — bảng liệt kê (dòng 35–36)

```text
VAL_ 512 DoorStatus 0 "AllClosed" 1 "DriverOpen" 2 "PassengerOpen" ... ;
      │      │      └──┬───────┘                                      │
      │      │         └─ cặp (raw, tên), lặp tới hết       dấu ; kết thúc
      │      └─ tên signal
      └─ ID message
```

Đây là thứ biến

```text
INFO can.rx  signal  name=DoorStatus  value=1  unit=
```

thành

```text
INFO can.rx  signal  name=DoorStatus  value=1  means=DriverOpen
```

Nhánh rẽ ở [can_rx/main.cpp:94-100](../apps/can_rx/main.cpp#L94).

**Điều cần biết:** `DoorStatus` thực chất là **bitfield** (chú thích dòng 33 nói
rõ: bit 0 tài xế, bit 1 hành khách, bit 2 sau-trái, bit 3 sau-phải), nhưng `VAL_`
chỉ liệt kê `0, 1, 2, 4, 8`. Mở đồng thời hai cửa → raw `3` →
[dbc.cpp:331-334](../libs/av_can/src/dbc.cpp#L331) `values.find(3)` không thấy →
trả chuỗi rỗng → rơi về nhánh in số. Đúng như thiết kế của `VAL_` (nó là
*enumeration*, không phải *bitmask*), nhưng cần biết khi đọc log.

---

## 10. Ba điều đáng lưu ý

### a) Thứ tự dòng quan trọng — parser chạy một lượt

`SG_` gắn vào `messages_.back()`
([dbc.cpp:296](../libs/av_can/src/dbc.cpp#L296)) nên phải đứng **sau** `BO_` của
nó. `VAL_` và `CM_` tìm message theo ID
([dbc.cpp:253](../libs/av_can/src/dbc.cpp#L253)) nên cũng phải đứng sau. Đưa
`VAL_` lên đầu file → vòng lặp không tìm thấy message → **bị bỏ im lặng, không
báo lỗi**. Format DBC thật cũng quy định thứ tự này, nhưng parser của ta không
cảnh báo.

### b) Range của `FuelLevel` mô tả *mã hoá*, không phải *vật lý*

```text
SG_ FuelLevel : 40|8@1+ (0.5,0) [0|127.5] "%" Cluster
```

`127.5` = 255 × 0.5, tức là **giá trị lớn nhất 8 bit biểu diễn được** — không phải
giới hạn kỹ thuật. Xăng không thể quá 100 %.

Theo đúng lập luận trong [`w06_dbc.md`](w06_dbc.md) §7 câu 5 — *bề rộng bit là một
sự thật về lưu trữ; range là một khẳng định kỹ thuật* — dòng này lẽ ra nên là
`[0|100]`. Hiện tại 120 % vẫn qua được `in_range`. Cùng vấn đề ở
`CoolantTemp [-40|215]`; `VehicleSpeed [0|655.35]` và `EngineRpm [0|16383.75]`
cũng vậy nhưng vô hại hơn.

**Bài tập:** sửa dòng 20 thành `[0|100]`, `sim_vehicle` sẽ từ chối gửi khi
simulator sinh giá trị > 100, với log `encode refused ... min=0 max=100`.

### c) File này KHÔNG nói gì về những thứ sau

| Không có trong DBC | Nằm ở đâu |
|---|---|
| Bitrate | `ip link set can0 type can bitrate 500000` |
| Chu kỳ gửi | chú thích `CM_`, hoặc `BA_` attribute |
| Ai được phép gửi ID nào (bảo mật) | không có — CAN không có khái niệm này |
| Arbitration, CRC, ACK, bus-off | phần cứng controller — [`w04_can_vi.md`](w04_can_vi.md) |
| Ý nghĩa khi signal *không đến* | logic ứng dụng — `kStaleAfter` ở [can_rx/main.cpp:59](../apps/can_rx/main.cpp#L59) |

---

## 11. Checklist tự kiểm

- [ ] `BO_ 2147483939` là ID gì? Vì sao?
- [ ] `CC 1C` với `@1` và với `@0` cho ra hai số nào?
- [ ] `CoolantTemp` dùng offset −40 để làm gì?
- [ ] Vì sao `FuelLevel` có max `127.5` mà không phải `100`?
- [ ] `DoorStatus = 3` thì `can_rx` in ra gì, và vì sao không có tên?
- [ ] Chu kỳ gửi 100 ms được ghi ở đâu trong file, và vì sao đó là vấn đề?
- [ ] Byte 6–7 của `EngineData` luôn bằng 0 — do đâu?
