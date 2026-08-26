# Tuần 1 — Bit, CAN frame và bộ khung xương sống

> Bản tiếng Việt của [w01_spine.md](w01_spine.md). Thuật ngữ kỹ thuật giữ
> nguyên tiếng Anh, vì đó là từ bạn sẽ gặp trong DBC, trong code, và trong
> phòng phỏng vấn.

## 1. Khái niệm

Một vehicle signal **không phải** là một con số nằm sẵn trong message. Nó là
một **quy tắc** để đọc một dãy bit:

```text
start_bit | length | byte order | signed? | factor | offset
```

Áp quy tắc lên payload → được một số nguyên thô (raw). Áp tiếp `factor` và
`offset` → được con số mà con người hiểu được. Làm ngược lại để truyền đi.

Chỗ thực sự khó duy nhất là **byte order**. DBC đánh số bit theo kiểu LSB-first
trong từng byte (bit 0 = byte 0 mask `0x01`, bit 8 = byte 1 mask `0x01`). Cả
hai layout dùng chung cách đánh số đó, chỉ khác nhau ở **hướng đi**:

| | `start_bit` là | cách đi |
|---|---|---|
| **Intel** (`@1`) | LSB của signal | +1, đi thẳng qua ranh giới byte |
| **Motorola** (`@0`) | MSB của signal | −1, trừ khi chạm ranh giới byte thì **+15** |

Con số +15 chính là toàn bộ mẹo: rời byte *k* tại bit 0 của nó (số hiệu 8k) và
bước vào byte *k+1* tại bit 7 (số hiệu 8k+15) — đúng 15 bước. Một dòng đó trong
[next_bit()](../libs/av_can/src/bit.cpp) là toàn bộ khác biệt giữa hai layout.

DLC là thứ thứ hai không phải như tên gọi. Nó là một **mã 4 bit**, không phải
số byte. Đến 8 thì hai thứ trùng nhau; CAN-FD tái sử dụng mã 9–15 cho 12, 16,
20, 24, 32, 48 và 64 byte. Nên **payload CAN-FD 13 byte không tồn tại** — stack
thật sẽ pad lên 16 và DBC khai báo message là 16.

## 2. Tại sao nó tồn tại trong Cockpit DC

Mọi con số trên cluster đều bắt đầu từ đây. Tốc độ, vòng tua, nhiệt độ nước
làm mát, trạng thái cửa — mỗi thứ là một dải bit trong một frame mà nhà cung
cấp đã ghi vào DBC, và một ECU nào đó encode theo chu kỳ cố định.

Hai hệ quả quan trọng về sau:

- **Byte order không phải chuyện hình thức.** ECU powertrain của các nhà cung
  cấp châu Âu gần như luôn dùng Motorola; module body/comfort thường dùng
  Intel. Một cockpit phải decode cả hai, trong cùng một process, bằng cùng một
  đoạn code.
- **Sai byte order thì im lặng hoàn toàn.** Mục 5 bên dưới chứng minh điều đó:
  cùng một chuỗi byte, đọc hai kiểu ra 82.5 km/h và 148.8 km/h — và cả hai đều
  là tốc độ hợp lý. Không CRC nào, không ACK nào, không error frame nào bắt
  được. Chỉ có plausibility check trên giá trị, hoặc so sánh trên bench với xe
  thật, mới phát hiện ra.

## 3. Kiến trúc

Tuần 1 xây hai hộp dưới cùng, phần còn lại để trống:

```text
                            [ Cluster UI ]        tuần 13
                                  |
                            [ Qt model ]          tuần 12
                                  |
                            [ Middleware ]        tuần 8-10
                                  |
                        [ Vehicle data model ]    tuần 8
                                  |
    +-----------------------------+-----------------------------+
    |                     Signal decode                         |  <- Ở ĐÂY
    |     SignalSpec + factor/offset + sign  (av/can/signal)    |
    +-----------------------------+-----------------------------+
    |                       Bit walking                         |  <- Ở ĐÂY
    |          Intel / Motorola  (av/can/bit)                   |
    +-----------------------------+-----------------------------+
                                  |
                            [ CanFrame ]                           <- Ở ĐÂY
                                  |
                        [ SocketCAN / vcan0 ]     tuần 5
```

`av_can` là library. Nó xuất hiện ở tuần 1 thay vì tuần 4 vì roadmap dựng bộ
khung xương sống trước (CLAUDE.md §14); SocketCAN và DBC parser thật sẽ được
gắn thêm vào chính library này ở tuần 5 và 6.

## 4. Hiện thực C++ / Linux

| File | Vai trò |
|---|---|
| [libs/av_can/include/av/can/bit.hpp](../libs/av_can/include/av/can/bit.hpp) | `ByteOrder`, `extract_bits`, `insert_bits`, `sign_extend` |
| [libs/av_can/src/bit.cpp](../libs/av_can/src/bit.cpp) | phần đi bit; `next_bit()` là toàn bộ khác biệt Intel/Motorola |
| [libs/av_can/include/av/can/frame.hpp](../libs/av_can/include/av/can/frame.hpp) | `CanFrame`, DLC ↔ length, `is_valid` |
| [libs/av_can/src/frame.cpp](../libs/av_can/src/frame.cpp) | bảng DLC thưa của CAN-FD |
| [libs/av_can/include/av/can/signal.hpp](../libs/av_can/include/av/can/signal.hpp) | `SignalSpec` — một dòng `SG_` của DBC |
| [libs/av_can/src/signal.cpp](../libs/av_can/src/signal.cpp) | `decode` / `encode`, factor/offset, từ chối khi ngoài dải |
| [apps/spine/main.cpp](../apps/spine/main.cpp) | demo chạy được, có 2 fault được tiêm vào |

### Quyết định về ownership và API

- **`extract_bits` trả `std::optional`, không trả 0.** Một signal không nằm vừa
  trong frame *không phải* là giá trị 0. Gộp hai thứ đó lại sẽ khiến một frame
  bị cắt ngắn hiển thị 0 km/h trên cluster — một con số hoàn toàn hợp lý, và vì
  thế là failure mode tệ nhất có thể có.
- **`insert_bits` duyệt hết đường đi trước khi ghi byte đầu tiên.** Một signal
  ghi dở sẽ decode ra không báo lỗi, thành một giá trị chưa từng được gửi. Từ
  chối luôn tốt hơn hẳn làm hỏng.
- **`encode` từ chối giá trị ngoài dải thay vì wrap.** 700 km/h không nhét vừa
  16 bit ở độ phân giải 0.01, nên bên phát nói không.
- **Chưa có min/max trong `SignalSpec`.** Range validation thuộc về DBC parser ở
  tuần 6. Một field tồn tại nhưng không bao giờ được kiểm tra còn tệ hơn là
  không có field, vì người đọc code sẽ mặc định là nó đã được check.

### Hai cái bẫy undefined behaviour mà code này né

Cả hai đều liên quan đến phép dịch bit, và cả hai đều là cố ý:

```cpp
// 1. Dịch một giá trị 64-bit đi 64 bước là UB, và signal 64-bit rơi trúng nó.
constexpr std::uint64_t low_mask(unsigned length) {
    return (length >= 64) ? ~std::uint64_t{0} : (std::uint64_t{1} << length) - 1U;
}

// 2. 2^63 - 1 không biểu diễn được bằng double, nên `scaled > 2^63 - 1` sẽ cho
//    lọt một giá trị mà phép cast sang int64 không nhận được. So với 2^63.
if (scaled < -limit || scaled >= limit) { return false; }
```

## 5. Bài tập thực hành

```bash
./check.sh fast              # build + 4 test binary
./build/debug/apps/spine/spine
```

Frame nó sinh ra:

```text
  vcan0  100   [8]  3A 20 0C 80 82 83 FF 00
                    \___/ \___/ \/ \___/
                     |     |     |   |
                     |     |     |   SteeringAngle -12.5 deg  (Intel, signed)
                     |     |     CoolantTemp 90 degC          (offset -40)
                     |     EngineRpm 800 rpm                  (Motorola!)
                     VehicleSpeed 82.5 km/h                   (Intel)
```

Đáng tính tay một lần, vì đây là lần cuối các con số còn đủ nhỏ để làm được:
`82.5 / 0.01 = 8250 = 0x203A`, Intel nên byte thấp trước → `3A 20`.
`800 / 0.25 = 3200 = 0x0C80`, Motorola nên byte cao trước → `0C 80`.

Format `vcan0 100 [8] ...` được chọn cố ý giống hệt `candump`, để output này và
output của tool thật khớp nhau ở tuần 5.

## 6. Bài tập gây lỗi / debug

| Fault được tiêm | Phát hiện bởi | Log ra | Fallback | Safe state |
|---|---|---|---|---|
| Frame bị cắt ngắn (8 → 2 byte) | vòng đi bit của `extract_bits` chạy ra ngoài payload | `WARN signal unavailable name=EngineRpm reason=does not fit frame` | chưa có — giá trị đơn giản là vắng mặt | tuần 8: giữ giá trị cuối, rồi đánh dấu stale sau timeout |
| Sai byte order trong dòng DBC | **không có gì** | `INFO intel=82.5 motorola=148.8` | không thể có | chỉ plausibility check / rate-of-change check mới bắt được |
| Encode ngoài dải (700 km/h) | kiểm tra dải trong `encode` | caller nhận `false` | frame không bị đụng tới, không truyền gì | bên phát từ chối thay vì wrap thành 44.6 km/h |
| Độ dài CAN-FD không encode được (13 byte) | `is_valid` qua `length_to_dlc` | caller nhận `false` | frame không được gửi | — |

**Dòng thứ hai mới là dòng quan trọng.** Đây là ví dụ đầu tiên trong project về
một fault **không có cơ chế phát hiện nào tại chính tầng nó xảy ra**. CRC, ACK
và error frame đều qua hết: bus đã giao đúng chính xác những byte được gửi. Lỗi
nằm ở phần *diễn giải*, và phòng thủ duy nhất là ở tầng cao hơn — giới hạn dải,
giới hạn tốc độ biến thiên, hoặc so sánh trên bench.

Đó cũng là câu trả lời thành thật cho câu hỏi "tại sao functional safety lại
quan tâm nhiều hơn một cái checksum" (tuần 14).

## 7. Câu hỏi phỏng vấn Senior

**1. Một dòng DBC ghi `SG_ EngineRpm : 23|16@0+ (0.25,0)`. Nó chiếm những byte
nào, và tại sao?**

Byte 2 và 3, big-endian. Start bit 23 là byte 2 bit 7 (23 = 2×8 + 7), tức MSB
của signal. Đường đi chạy 23→16 xuống hết byte 2, rồi cú nhảy +15 đáp xuống bit
31 = byte 3 bit 7, rồi tiếp tục 31→24. Vậy `raw = (data[2] << 8) | data[3]`, và
giá trị vật lý là `raw × 0.25` rpm.

**2. Cluster của bạn hiện 148.8 km/h trong khi xe đang chạy 82.5. Bạn nhìn vào
đâu đầu tiên?**

Dòng DBC, không phải UI. Tỷ lệ đó chính là dấu hiệu đặc trưng của byte-swap:
`0x203A` đọc kiểu Intel là 8250, đọc kiểu Motorola thành `0x3A20` = 14880. Bất
cứ khi nào hai cách đọc liên hệ với nhau bằng một phép hoán vị byte thì vấn đề
nằm ở `@0` vs `@1`, chứ không phải ở bus, ở driver, hay ở binding. Xác nhận
bằng cách decode tay một frame thô lấy từ `candump`.

**3. Tại sao `extract_bits` trả `std::optional` thay vì trả 0?**

Vì "chưa nhận được" và "nhận được giá trị 0" phải đến được cluster như hai thứ
khác nhau. Một frame bị cắt ngắn decode ra 0 km/h thì không phân biệt nổi với
một chiếc xe đang đứng yên. `optional` ép mọi caller phải ra quyết định đó một
cách tường minh — và đó chính là sự phân biệt mà middleware ở tuần 8 cần để
hiện thực staleness và safe-state fallback.

**4. Tại sao một CAN-FD frame không thể mang 13 byte?**

DLC là mã 4 bit, không phải độ dài. Mã 0–8 là số byte; CAN-FD tái sử dụng 9–15
cho bước nhảy 12/16/20/24/32/48/64. Không có mã nào cho 13, nên bên phát pad
lên 16 và DBC khai báo 16.

**5. `sign_extend` cast một `uint64_t` có bit cao nhất bằng 1 sang `int64_t`.
Cái đó có phải undefined không?**

Không phải undefined — mà là *implementation-defined* trong C++17 (được định
nghĩa rõ từ C++20, chuẩn này bắt buộc two's complement). Mọi target mà project
này chạy đều là two's complement, và comment trong code đã nói vậy. Đáng nêu ra
khi review thay vì để im: cùng pattern đó là bug portability thật trên một máy
sign-magnitude — chính vì thế C++20 đã xoá bỏ sự mơ hồ này.

## 8. Checklist tự đánh giá

- [x] Tôi giải thích được mà không cần nhìn note
- [x] Tôi tự hiện thực, không chỉ đọc
- [x] Tôi cố tình phá nó và quan sát nó hỏng — frame cắt ngắn, đảo byte order,
      encode ngoài dải
- [x] Tôi bảo vệ được trade-off thiết kế đã chọn — `optional` thay vì sentinel 0,
      từ chối thay vì wrap, validate trước khi ghi
- [x] Test pass dưới `./check.sh` — debug / asan / ubsan / tsan / clang-tidy sạch

### Cố ý để lại cho sau

- Kiểm tra dải (`[min|max]`) → tuần 6, cùng với DBC parser
- Multiplexed signal (`SG_MUL_VAL_`) → tuần 6
- Đi từng bit là O(n) cho mỗi signal; decoder production dùng mask theo byte.
  Đúng trước đã, và 64 vòng lặp không phải là nút thắt ở tần số 100 Hz.
