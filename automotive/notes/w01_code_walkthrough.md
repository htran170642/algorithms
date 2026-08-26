# Tuần 1 — Đọc code: từng file, và tại sao lại viết như vậy

> Viết bằng tiếng Việt, thuật ngữ giữ nguyên tiếng Anh.
> Đây là phần bổ sung cho [w01_spine.md](w01_spine.md) / [w01_spine_vi.md](w01_spine_vi.md):
> file kia nói *khái niệm*, file này nói *tại sao code lại như thế*.

Thứ tự đọc theo phụ thuộc — mỗi file chỉ dùng thứ đã giải thích trước đó:

```text
bit.hpp/cpp   ──┐
                ├──> signal.hpp/cpp ──> main.cpp
frame.hpp/cpp ──┘
```

---

# 1. `bit.hpp` — bản hợp đồng

[../libs/av_can/include/av/can/bit.hpp](../libs/av_can/include/av/can/bit.hpp)

```cpp
enum class ByteOrder : std::uint8_t { Intel, Motorola };
```

**Tại sao `enum class` chứ không phải `enum`?** `enum` thường rò tên ra namespace
bao ngoài (`Intel` trở thành tên toàn cục) và tự động chuyển ngầm sang `int`.
Nghĩa là `extract_bits(data, 8, 0, 16, 1)` sẽ **compile được** — số 1 âm thầm
thành `Motorola`. `enum class` chặn cả hai: bạn buộc phải viết
`ByteOrder::Motorola`.

**Tại sao `: std::uint8_t`?** Hai lý do. Thứ nhất, kích thước enum được cố định
— quan trọng khi struct chứa nó đi qua ranh giới ABI hoặc được đóng gói. Thứ
hai, `enum` không ghi rõ underlying type thì compiler tự chọn (thường `int`,
4 byte), làm `SignalSpec` phình ra vô ích. Đây là thói quen tốt trong
automotive, nơi struct thường bị đếm từng byte.

```cpp
inline constexpr unsigned kMaxSignalBits = 64;
```

**Tại sao `inline`?** Đây là tính năng C++17. Không có `inline`, mỗi translation
unit include header này sẽ có **một bản sao riêng** của biến — với `constexpr`
thì thường vô hại, nhưng nếu ai đó lấy địa chỉ `&kMaxSignalBits` thì hai TU cho
ra hai địa chỉ khác nhau, vi phạm ODR. `inline` đảm bảo cả chương trình chỉ có
đúng một thực thể.

## Quyết định gây tranh cãi nhất: chữ ký hàm

```cpp
std::optional<std::uint64_t> extract_bits(const std::uint8_t* data, std::size_t size,
                                          unsigned start_bit, unsigned length,
                                          ByteOrder order) noexcept;
```

**Tại sao `const uint8_t* + size` mà không phải `const CanFrame&`?** Đây là
quyết định về **phân tầng**. Nếu `bit.hpp` include `frame.hpp` thì tầng thao tác
bit phụ thuộc vào khái niệm "CAN frame". Nhưng đi bit là thao tác thuần tuý
trên byte — nó cũng đúng cho payload SOME/IP ở tuần 8, cho vùng shared memory ở
tuần 3. Giữ nó không biết gì về CAN nghĩa là tái dùng được.

Hệ quả nằm ở [signal.cpp](../libs/av_can/src/signal.cpp): chính `signal.cpp` mới
là chỗ nối hai tầng lại (`frame.data.data(), frame.length`).

**Tại sao không `std::span`?** Vì `std::span` là C++20. CLAUDE.md §8 chốt C++17.
Nếu được dùng C++20 thì `std::span<const std::uint8_t>` sẽ tốt hơn hẳn — gộp
con trỏ và kích thước lại, không thể truyền lệch nhau.

**Tại sao `std::optional` mà không phải trả `0` hay dùng exception?**

- Trả `0`: đây là lỗi thiết kế nguy hiểm nhất có thể mắc ở đây. "Frame bị cắt
  ngắn" và "tốc độ bằng 0" phải là hai thứ khác nhau trên cluster. Nếu gộp lại,
  một chiếc xe đang chạy 80 km/h mà mất frame sẽ hiện số 0 — hoàn toàn hợp lý
  về mặt thị giác, không ai nghi ngờ.
- Exception: automotive tránh exception trong đường dữ liệu vì chi phí không
  xác định được (WCET — tuần 14). Ngoài ra hàm này `noexcept`.
- `std::optional` ép caller phải xử lý. `if (!raw) return std::nullopt;` không
  thể quên được như một giá trị `0` trả về.

**Tại sao `noexcept`?** Ba lợi ích: compiler sinh code tốt hơn (không cần unwind
table cho đường này), nó là lời hứa với người đọc rằng hàm không cấp phát /
không ném, và nó là điều kiện cần cho phân tích WCET sau này.

---

# 2. `bit.cpp` — phần khó nhất trong cả tuần

[../libs/av_can/src/bit.cpp](../libs/av_can/src/bit.cpp)

## 2.1 Anonymous namespace

```cpp
namespace av::can {
namespace {          // <- không tên
constexpr std::size_t byte_index(std::size_t bit) noexcept { return bit / 8U; }
```

**Tại sao `namespace {}` mà không phải `static`?** Cả hai cho internal linkage.
Nhưng `static` chỉ áp dụng cho hàm/biến, còn anonymous namespace áp dụng được
cho cả `struct`, `enum`, template. Dùng một cơ chế nhất quán thì dễ đọc hơn.
Đây cũng là khuyến nghị của C++ Core Guidelines.

Hệ quả thực tế: `byte_index` không xuất hiện trong symbol table, không đụng tên
với file khác, và linker được tự do inline nó hoàn toàn.

## 2.2 Cách DBC đánh số bit — vẽ ra thì hiểu ngay

Đây là chỗ mọi người vấp. Vẽ mỗi byte với MSB bên trái:

```text
          bit 7  6  5  4  3  2  1  0     <- vị trí trong byte
byte 0:      7  6  5  4  3  2  1  0      <- số hiệu DBC
byte 1:     15 14 13 12 11 10  9  8
byte 2:     23 22 21 20 19 18 17 16
byte 3:     31 30 29 28 27 26 25 24
```

Hai công thức trong code chỉ là đọc bảng này:

```cpp
byte_index(bit) = bit / 8         // dòng nào
bit_mask(bit)   = 1 << (bit % 8)  // cột nào
```

Bit 23 → dòng `23/8 = 2`, mask `1 << (23%8) = 1<<7 = 0x80`. Đúng: byte 2, MSB.

Giờ nhìn bảng và tự hỏi **hai layout đi theo đường nào**:

```text
Motorola bắt đầu ở 7:   7 → 6 → 5 → 4 → 3 → 2 → 1 → 0
                                                     ↓
                       15 → 14 → 13 → 12 → 11 → 10 → 9 → 8

  ==> đọc trái-sang-phải, trên-xuống-dưới. Liên tục trên bảng.

Intel bắt đầu ở 0:      0 → 1 → 2 → 3 → 4 → 5 → 6 → 7
                                                     ↓
                        8 →  9 → 10 → 11 → 12 → 13 → 14 → 15

  ==> đọc phải-sang-trái trong từng dòng.
```

**Motorola là cách đọc tự nhiên khi vẽ bảng như trên.** Đó là lý do nó tồn tại —
nó "hợp lý" với người thiết kế hardware nhìn frame như một chuỗi bit liên tục.
Intel "hợp lý" với người viết phần mềm trên máy little-endian.

## 2.3 `next_bit` — một dòng chứa toàn bộ khác biệt

```cpp
constexpr std::size_t next_bit(std::size_t bit, ByteOrder order) noexcept {
    if (order == ByteOrder::Intel) {
        return bit + 1U;
    }
    return (bit % 8U == 0U) ? bit + 15U : bit - 1U;
}
```

Intel thì hiển nhiên: +1.

Motorola: đi xuống trong byte (−1) cho đến khi chạm bit 0 của byte đó. Bit 0 của
byte *k* mang số hiệu `8k`, tức `bit % 8 == 0`. Bit kế tiếp phải là bit 7 của
byte *k+1*, mang số hiệu `8(k+1) + 7 = 8k + 15`. Chênh lệch: **+15**.

Kiểm chứng bằng bảng trên: đang ở 0 (byte 0, bit 0) → tiếp theo phải là 15
(byte 1, bit 7). `0 + 15 = 15` ✓. Đang ở 16 (byte 2, bit 0) → tiếp theo 31.
`16 + 15 = 31` ✓.

**Tại sao viết dưới dạng hàm `constexpr` nhỏ thay vì nhét thẳng vào vòng lặp?**
Vì nó là *khái niệm* — "bit kế tiếp". Đặt tên cho nó biến một dòng khó hiểu
thành một dòng có tên. Compiler inline hoàn toàn nên không tốn gì.

## 2.4 `value_shift` — nửa còn lại của mẹo, chỗ hay bị bỏ sót

```cpp
constexpr unsigned value_shift(unsigned index, unsigned length, ByteOrder order) noexcept {
    return (order == ByteOrder::Intel) ? index : (length - 1U - index);
}
```

Đây là chỗ **rất nhiều implementation làm sai**. Có **hai** quyết định độc lập,
và cả hai đều đảo khi đổi layout:

| | bit frame kế tiếp là gì | bit đó đi vào đâu trong raw value |
|---|---|---|
| Intel | `bit + 1` | bit thứ `i` (LSB trước) |
| Motorola | `bit - 1` hoặc `+15` | bit thứ `length-1-i` (MSB trước) |

Nếu bạn làm đúng cột trái mà quên cột phải, kết quả là **giá trị bị đảo ngược
thứ tự bit** — không phải sai lệch nhỏ, mà là một con số hoàn toàn khác. Test
`RoundTrip.EveryPlacementThatFitsSurvives` chính là để bắt loại lỗi này: nó thử
`2 layout × 32 length × 64 start = 4096` tổ hợp.

## 2.5 `geometry_is_sane` và một điểm gợn cần sửa

```cpp
constexpr bool geometry_is_sane(std::size_t size, unsigned length) noexcept {
    return length != 0U && length <= kMaxSignalBits && size <= kMaxSignalBits;
}
```

- `length != 0`: signal 0 bit vô nghĩa.
- `length <= 64`: raw value là `uint64_t`, không chứa nổi hơn.
- `size <= kMaxSignalBits`: **chặn tràn số** ở dòng `size * 8U` phía dưới. Nếu
  ai đó truyền `size = SIZE_MAX`, phép nhân sẽ wrap và mọi kiểm tra biên sau đó
  thành vô nghĩa.

**Điểm gợn:** dòng thứ ba đọc rất kỳ. Ý nghĩa của nó là "payload không quá 64
**byte**", nhưng nó lại so với `kMaxSignalBits` — hằng số nói về **bit**. Hai
con số tình cờ đều bằng 64 (signal tối đa 64 bit, payload CAN-FD tối đa 64
byte). Đúng về mặt hành vi, nhưng sai về mặt ngữ nghĩa, và nếu sau này ai đó
đổi một trong hai thì bug xuất hiện im lặng.

Cách sửa đúng là khai báo riêng trong `bit.hpp`:

```cpp
/// Payload lớn nhất tầng này chấp nhận, tính bằng byte. Trùng số với
/// kMaxPayload của CAN-FD, nhưng là một khái niệm khác.
inline constexpr std::size_t kMaxBufferBytes = 64;
```

Để nguyên thì không sai hành vi, nhưng nên sửa — đây đúng loại nhận xét sẽ xuất
hiện trong code review thật.

## 2.6 `extract_bits` — tại sao kiểm tra biên **bên trong** vòng lặp

```cpp
for (unsigned i = 0U; i < length; ++i) {
    if (bit >= total) {          // <- mỗi bước, không phải một lần
        return std::nullopt;
    }
    ...
}
```

Với Intel bạn có thể kiểm tra một lần: `start_bit + length <= total`. Với
Motorola thì **không**, vì đường đi không đơn điệu — nó nhảy tới nhảy lui.

Ví dụ cụ thể: Motorola, `start_bit = 63`, `length = 16`, frame 8 byte
(`total = 64`). Start bit 63 nằm gọn trong frame. Đường đi: 63, 62, ..., 56, rồi
`56 % 8 == 0` → nhảy lên `56 + 15 = 71`. **71 ≥ 64** — đã ra ngoài. Một phép
kiểm tra ở đầu hàm sẽ bỏ lọt.

Test `Geometry.SignalRunningOffTheEndIsRejected` khoá đúng trường hợp này.

## 2.7 `insert_bits` — tại sao duyệt **hai lần**

```cpp
// Lượt 1: chỉ kiểm tra, không ghi
for (...) { if (bit >= total) return false; bit = next_bit(bit, order); }

// Lượt 2: ghi thật
bit = start_bit;
for (...) { ... }
```

Đây là **atomicity ở mức API**: hoặc toàn bộ signal được ghi, hoặc không byte
nào bị đụng.

Tại sao quan trọng? Giả sử ghi một nửa rồi phát hiện lỗi và trả `false`. Caller
có thể bỏ qua giá trị trả về (rất dễ xảy ra), frame vẫn được gửi đi, và bên nhận
decode nó ra **một con số hợp lệ chưa từng tồn tại**. Không có CRC nào bắt được
— bytes đúng như đã gửi.

So sánh: từ chối thẳng thì tệ nhất là mất một chu kỳ dữ liệu, và tầng trên phát
hiện được qua timeout.

Test `RoundTrip.InsertRejectsAValueThatDoesNotFitTheFrame` so sánh toàn bộ mảng
trước/sau để chứng minh điều này.

**Cái giá:** duyệt hai lần. Với ≤64 bit thì không đáng kể, và tuần 6 có thể tối
ưu thành mask theo byte nếu profiler chỉ ra.

## 2.8 Read-modify-write, không phải ghi đè

```cpp
if (((value >> value_shift(i, length, order)) & 1U) != 0U) {
    byte = static_cast<std::uint8_t>(byte | mask);        // set
} else {
    byte = static_cast<std::uint8_t>(byte & static_cast<std::uint8_t>(~mask));  // clear
}
```

Phải có **cả hai nhánh**. Nếu chỉ `|= mask` khi bit là 1 và không làm gì khi bit
là 0, thì ghi đè một signal chỉ có thể *thêm* bit — giá trị cũ 0xFF ghi đè bằng
0x00 sẽ vẫn là 0xFF.

Test `Encode.PacksSignalsWithoutDisturbingEachOther` ghi 82.5 rồi ghi đè 0.0 và
kiểm tra đúng ra 0.0, đồng thời signal bên cạnh không đổi.

**Tại sao lắm `static_cast` vậy?** Vì `-Wconversion -Wsign-conversion -Werror`
(bật từ tuần 0). Trong C++, `byte | mask` với `byte` là `uint8_t` sẽ
**integer-promote** cả hai lên `int`, cho kết quả `int`. Gán ngược về `uint8_t`
là thu hẹp → warning → lỗi build. `~mask` còn tệ hơn: `~` promote `uint8_t` lên
`int`, `~0x04` thành `0xFFFFFFFB` (int), nên phải cast về `uint8_t` trước khi `&`.

Đây không phải chuyện làm hài lòng compiler. Integer promotion là nguồn bug thật
trong code nhúng, và `-Wconversion` được bật đúng vì tuần 1 là tuần làm việc với
bit.

## 2.9 `sign_extend` — hai cái bẫy UB

```cpp
if (length == 0U || length >= kMaxSignalBits) {
    return static_cast<std::int64_t>(raw);
}
```

**Bẫy 1:** với `length == 64`, dòng `std::uint64_t{1} << length` là
**undefined behaviour** — chuẩn C++ quy định dịch một giá trị `N` bit đi `≥ N`
bước là UB. Không phải "trả về 0", mà là UB thật: trên x86 lệnh `shl` chỉ dùng
6 bit thấp của số đếm, nên `<< 64` thực tế thành `<< 0`, trả về `1`. Sai âm thầm.

Với `length == 64` thì cũng chẳng có gì để mở rộng dấu — toàn bộ 64 bit đã là
giá trị. Nên return sớm vừa đúng ngữ nghĩa vừa né UB.

```cpp
const std::uint64_t fill = ~((std::uint64_t{1} << length) - 1U);
return static_cast<std::int64_t>(raw | fill);
```

**Bẫy 2:** `raw | fill` có bit 63 bằng 1, tức giá trị > `INT64_MAX`. Cast một
`uint64_t` ngoài dải sang `int64_t`:

- **C++17:** implementation-defined. Compiler *phải* tài liệu hoá hành vi, nhưng
  chuẩn không ép cụ thể.
- **C++20:** well-defined, vì C++20 bắt buộc two's complement.

GCC/Clang trên x86/ARM đều làm điều bạn muốn (giữ nguyên bit pattern). Comment
trong code nói rõ điều đó. Đây là loại chỗ nên viết comment thay vì để im —
người review sau sẽ hỏi.

---

# 3. `frame.hpp` / `frame.cpp`

## 3.1 Tại sao `std::array` chứ không phải `std::vector`

```cpp
struct CanFrame {
    std::uint32_t id{};
    std::uint8_t length{};
    bool extended{false};
    bool fd{false};
    bool brs{false};
    std::array<std::uint8_t, kMaxPayload> data{};
};
```

`std::vector<uint8_t>` sẽ tiết kiệm bộ nhớ cho frame 8 byte. Nhưng:

- **Cấp phát heap là không xác định về thời gian.** `new` có thể phải xin memory
  từ kernel. Trong đường nhận CAN chạy 1000 frame/giây, đó là biến thiên latency
  không kiểm soát được → hỏng phân tích WCET (tuần 14).
- **Copy `CanFrame` trở thành thao tác cấp phát.** Ở tuần 2 frame sẽ đi qua
  queue giữa các thread; copy phải là `memcpy` thuần.
- **72 byte** là không đáng gì. Xe nào cũng thừa RAM cho vài nghìn frame.

Đây là mẫu chung trong automotive: **kích thước cố định, không heap, trong mọi
thứ nằm trên đường dữ liệu**.

`{}` sau mỗi member là **default member initializer** (C++11). Nó đảm bảo
`CanFrame frame{};` khởi tạo về 0 hết. Không có nó, `CanFrame frame;` để lại rác
— và một `id` rác thì frame đi sai địa chỉ.

## 3.2 Tại sao lưu `length` chứ không lưu `dlc`

DLC là mã trên đường truyền; `length` là số byte thật. Lưu `length` vì:

1. **99% code chỉ quan tâm số byte.** `for (i < frame.length)` là thứ bạn viết
   khắp nơi.
2. **Không phải mọi DLC đều hợp lệ.** DLC 13 với classic CAN nghĩa là gì? Lưu
   `length` thì câu hỏi đó biến mất.
3. **SocketCAN cũng làm vậy.** `struct canfd_frame` có field `len` là số byte.
   Tuần 5 sẽ map 1-1.

Đổi lại, phải có hàm chuyển đổi tường minh — và chính chúng dạy bạn khái niệm:

```cpp
std::uint8_t dlc_to_length(std::uint8_t dlc, bool fd) noexcept {
    if (dlc <= kClassicMaxPayload) return dlc;         // 0..8: mã = số byte
    if (!fd || dlc > kMaxDlc) return 8;                // classic: bão hoà ở 8
    return kFdLengths[dlc - kFirstFdDlc];              // FD: bảng thưa
}
```

**Tại sao classic bão hoà ở 8 thay vì báo lỗi?** Vì đó là hành vi của
**hardware thật**. Một CAN controller classic nhận DLC 15 sẽ truyền 8 byte,
không báo lỗi. Hàm này mô phỏng phần cứng, không áp đặt luật riêng. Nếu bạn muốn
từ chối thì đó là việc của `is_valid`, ở tầng cao hơn.

## 3.3 Bảng DLC thưa — tại sao 13 byte không tồn tại

```cpp
constexpr std::array<std::uint8_t, 7> kFdLengths{12U, 16U, 20U, 24U, 32U, 48U, 64U};
```

DLC chỉ có 4 bit → 16 giá trị. Mã 0–8 dùng cho 0–8 byte. Còn lại 7 mã (9–15)
cho các độ dài lớn. CAN-FD chọn 7 con số trên. Không có mã cho 13, 14, 15... byte.

Hệ quả thực tế bạn sẽ gặp: nếu một message cần 13 byte, kỹ sư mạng phải khai báo
nó là **16 byte** trong DBC và 3 byte cuối là padding. Test
`Dlc.LengthsBetweenTheFdStepsCannotBeSent` khoá điều này.

```cpp
const auto* const found = std::find(kFdLengths.begin(), kFdLengths.end(), length);
```

`const auto* const` chứ không `const auto` là do clang-tidy
(`readability-qualified-auto`): với `std::array` trên libstdc++, `const_iterator`
**chính là** `const T*`. Viết `auto` che giấu việc đây là con trỏ. Quy tắc: nếu
kiểu suy ra là con trỏ, viết `auto*` để người đọc thấy.

## 3.4 `is_valid` — ba bất biến

```cpp
bool is_valid(const CanFrame& frame) noexcept {
    const std::uint32_t mask = frame.extended ? kExtendedIdMask : kStandardIdMask;
    if ((frame.id & ~mask) != 0U) return false;      // id vừa định dạng của nó
    if (frame.brs && !frame.fd) return false;        // BRS chỉ tồn tại trong FD
    return length_to_dlc(frame.length, frame.fd).has_value();  // độ dài gửi được
}
```

`(id & ~mask) != 0` là cách kiểm tra "có bit nào nằm ngoài mask không". Với
standard id, mask là `0x7FF` (11 bit), `~mask` là mọi bit từ 11 trở lên. Nếu id
có bit nào ở đó → không vừa.

BRS (Bit Rate Switch) là cờ chuyển sang tốc độ cao cho phần data của frame
CAN-FD. Classic CAN không có khái niệm này. Một frame có `brs=true, fd=false` là
**mâu thuẫn nội tại** — không phải giá trị sai, mà là trạng thái không tồn tại
được. Bắt ở đây tốt hơn để driver từ chối lúc runtime.

---

# 4. `signal.hpp` / `signal.cpp`

## 4.1 `SignalSpec` — dữ liệu, không phải hành vi

```cpp
struct SignalSpec {
    std::string_view name;
    unsigned start_bit{};
    unsigned length{};
    ByteOrder order{ByteOrder::Intel};
    bool is_signed{false};
    double factor{1.0};
    double offset{0.0};
    std::string_view unit;
};
```

Đây là struct thuần dữ liệu, và `decode`/`encode` là **hàm tự do** chứ không
phải method. Tại sao?

Vì `SignalSpec` sẽ đến từ **file DBC** ở tuần 6 — nó là dữ liệu cấu hình, không
phải object có hành vi. Giữ nó là aggregate cho phép:

- `constexpr` khởi tạo — bảng signal nằm trong `.rodata`, không tốn code khởi
  tạo lúc chạy.
- Copy / so sánh tầm thường.
- Parser tuần 6 chỉ cần điền field, không cần biết gì về `decode`.

**`std::string_view` — lợi và nguy hiểm.** Lợi: không cấp phát, `constexpr`
được, so sánh rẻ. Nguy hiểm: nó **không sở hữu** chuỗi. Ở đây an toàn vì các
spec là string literal (thời gian sống toàn chương trình). Nhưng ở tuần 6 khi
parser đọc từ file, spec phải trỏ vào một buffer mà parser giữ sống — nếu buffer
đó chết trước spec thì bạn có dangling `string_view`. **Đây là việc phải xử lý ở
tuần 6**, và là bug kinh điển với `string_view`.

**`factor{1.0}` và `offset{0.0}` mặc định** là "identity" — spec khai báo thiếu
vẫn cho ra raw value chứ không cho ra 0.

## 4.2 `decode` — ngắn vì công việc nằm ở tầng dưới

```cpp
std::optional<double> decode(const SignalSpec& spec, const CanFrame& frame) noexcept {
    const auto raw = extract_bits(frame.data.data(), frame.length, spec.start_bit,
                                  spec.length, spec.order);
    if (!raw) return std::nullopt;

    const double value = spec.is_signed
                             ? static_cast<double>(sign_extend(*raw, spec.length))
                             : static_cast<double>(*raw);
    return (value * spec.factor) + spec.offset;
}
```

Chú ý dòng `frame.length` — **không phải** `frame.data.size()`. `data` luôn là
64 byte, nhưng chỉ `length` byte đầu là dữ liệu thật. Truyền `data.size()` sẽ
khiến một signal ở byte 30 decode thành công từ một frame 8 byte, đọc rác. Đây
là dòng dễ viết sai nhất trong file, và test `Decode.TruncatedFrameYieldsNothing`
khoá nó.

Toàn bộ `decode` là ba bước: lấy bit → diễn giải dấu → áp thang đo. Ngắn vì phần
khó đã nằm trong `bit.cpp`. Đó là dấu hiệu phân tầng đúng.

## 4.3 `encode` — chỗ số thực gây rắc rối

```cpp
const double scaled = std::round((physical - spec.offset) / spec.factor);
```

**Tại sao `std::round` chứ không để cast tự cắt?** Cast `double → int` cắt về 0
(`static_cast<int>(2.7) == 2`). Với factor 0.25, giá trị 800.1 → `3200.4` → cắt
thành 3200 → decode ra 800.0. Có vẻ ổn. Nhưng 800.24 → `3200.96` → cắt thành
3200 → 800.0, trong khi làm tròn đúng phải ra 3201 → 800.25. Sai lệch tích luỹ
nửa bước lượng tử. `std::round` cho sai số tối đa nửa bước, đúng nghĩa "giá trị
biểu diễn được gần nhất".

### Chỗ tinh tế nhất trong cả tuần

```cpp
const double limit = std::ldexp(1.0, static_cast<int>(spec.length) - 1);
if (scaled < -limit || scaled >= limit) {
    return false;
}
```

Với signal signed `n` bit, dải hợp lệ là `[-2^(n-1), 2^(n-1) - 1]`. Cách viết
"hiển nhiên" là:

```cpp
if (scaled < -limit || scaled > limit - 1.0) return false;   // SAI ở n=64
```

Tại sao sai? Với `n = 64`, `limit - 1.0` phải là `2^63 - 1`. Nhưng `double` chỉ
có 53 bit mantissa — **`2^63 - 1` không biểu diễn được**. Phép `limit - 1.0` trả
về đúng `2^63` (trừ 1 không thay đổi gì ở độ chính xác đó). Nên điều kiện thành
`scaled > 2^63`, và một giá trị `scaled == 2^63` **lọt qua**. Rồi
`static_cast<int64_t>(2^63)` là undefined behaviour — `2^63` vượt `INT64_MAX`.

Viết `scaled >= limit` (so với `2^63`, một con số biểu diễn chính xác được) thì
chặn đúng, và mọi giá trị lọt qua đều `< 2^63`, cast an toàn.

**`std::ldexp(1.0, k)` thay vì `1 << k`** vì `1 << 64` là UB, còn `ldexp` tính
`1.0 × 2^k` trong miền số thực, không có giới hạn 64 bit.

Đây là loại lỗi chỉ lộ ra ở biên và không bao giờ xuất hiện trong test thông
thường. Test `Encode.RejectsSignedValuesOutsideTwosComplementRange` kiểm tra ở
`n=16` (3276.7 vs 3276.8), nhưng lý do viết code như vậy là vì `n=64`.

```cpp
raw = static_cast<std::uint64_t>(static_cast<std::int64_t>(scaled)) & low_mask(spec.length);
```

Cast hai chặng: `double → int64_t` (lấy giá trị có dấu), rồi `int64_t → uint64_t`
(lấy bit pattern two's complement), rồi `& low_mask` cắt còn `n` bit. Với −125 và
n=16: `int64_t(-125)` → bit pattern `0xFFFF...FF83` → `& 0xFFFF` → `0xFF83`.
Đúng là two's complement 16 bit của −125.

`low_mask` tách ra riêng cũng vì bẫy dịch-64-bước như `sign_extend`.

---

# 5. `apps/spine/main.cpp`

[../apps/spine/main.cpp](../apps/spine/main.cpp)

## 5.1 Tại sao gộp thành `Measurement` thay vì hai mảng song song

Bản đầu tiên viết:

```cpp
constexpr SignalSpec kEngineData[] = { ... };   // 4 phần tử
constexpr double kMeasured[] = {82.5, 800.0, 90.0, -12.5};
for (size_t i = 0; i < 4; ++i) encode(kEngineData[i], kMeasured[i], frame);
```

clang-tidy phản đối chỉ số chạy, và khi sửa mới lộ ra vấn đề thật sự lớn hơn:
**hai mảng song song có thể lệch nhau**. Thêm một signal vào mảng đầu mà quên
mảng sau → đọc ngoài biên, hoặc tệ hơn, ghép sai giá trị vào sai signal.
Compiler không phát hiện được.

```cpp
struct Measurement {
    SignalSpec spec;
    double value{};
};
constexpr std::array<Measurement, 4> kEngineData{{ ... }};
```

Giờ hai thứ **không thể** lệch — chúng là cùng một object. Nguyên tắc chung: nếu
hai dữ liệu phải khớp chỉ số với nhau, chúng nên là một struct.

**`{{` hai lớp** là vì `std::array` là aggregate bọc một C array bên trong: lớp
ngoài cho `std::array`, lớp trong cho mảng.

```cpp
for (const auto& [spec, measured] : kEngineData) {
```

Structured binding (C++17). Tương đương `m.spec`, `m.value` nhưng đọc gọn hơn và
không cần đặt tên biến trung gian.

## 5.2 `candump_line` — format không phải ngẫu nhiên

```cpp
os << "  vcan0  " << std::hex << std::setw(3) << std::setfill('0') << frame.id
   << "   [" << std::dec << unsigned{frame.length} << "] ";
```

Format này copy đúng `candump` của `can-utils`. Lý do: tuần 5 bạn sẽ chạy
`candump vcan0` song song với chương trình này. Nếu format khác nhau, so sánh
bằng mắt trở nên khó chịu. Nếu giống, bạn dán hai output cạnh nhau và thấy ngay
chỗ lệch.

**`unsigned{frame.length}`** thay vì `frame.length`: `uint8_t` là
`unsigned char`, và `operator<<` cho `char` in ra **ký tự**, không phải số.
`frame.length == 8` sẽ in ra backspace. Bọc vào `unsigned{}` ép đi nhánh in số
nguyên. Đây là bug kinh điển với `uint8_t` và iostream.

Cú pháp `unsigned{x}` là **braced init**, khác `unsigned(x)`: nó cấm thu hẹp.
Nếu `x` là `int` thì không compile — buộc bạn dùng `static_cast` và nói rõ ý định.

## 5.3 Tại sao phải `std::flush`

```cpp
void heading(const char* text) { std::cout << '\n' << text << '\n' << std::flush; }
```

`av::log` ghi ra **stderr**, `heading` ghi ra **stdout**. Hai stream có chính
sách buffer khác nhau: stderr thường không buffer, stdout buffer theo dòng khi
ra terminal nhưng **buffer theo block khi bị pipe**. Chạy `./spine | tee log.txt`
mà không flush thì toàn bộ heading dồn xuống cuối, log ra trước — output đọc
thành vô nghĩa.

Lỗi này lộ ra ngay lần chạy demo đầu tiên (output bị đảo), và comment trong code
ghi lại lý do.

**Tại sao `std::flush` chứ không `std::endl`?** `std::endl` = `'\n'` + flush,
nhưng clang-tidy có check `performance-avoid-endl` vì người ta hay dùng `endl`
theo thói quen ở chỗ không cần flush. Viết `'\n' << std::flush` thể hiện rõ "tôi
cố ý flush ở đây".

## 5.4 Hai fault được tiêm — mục đích sư phạm

Fault 1 (frame cắt ngắn) có thể phát hiện được, và code phản ứng đúng: log
`WARN`, không bịa giá trị.

Fault 2 (sai byte order) **không thể phát hiện được ở tầng này**, và demo nói
thẳng điều đó:

```cpp
log::warn("rx.swapped", "nothing detects this", "note",
          "both readings are in range; only a plausibility check would catch it");
```

Nó có mặt trong demo vì đây là bài học quan trọng nhất tuần 1: **có những lỗi
không có cơ chế phát hiện tại tầng chúng xảy ra**. Đây là nền cho toàn bộ tuần
14 (ISO 26262) — lý do functional safety cần nhiều hơn checksum.

---

# 6. Test — mỗi nhóm chứng minh điều gì

| Nhóm | Chứng minh |
|---|---|
| `IntelLayout.*`, `MotorolaLayout.*` | Giá trị cụ thể, tính tay được. Nếu sai, sai ngay từ khái niệm. |
| `ByteOrderMatters.SameBytesDecodeDifferently` | Hai layout **thật sự** khác nhau. |
| `RoundTrip.EveryPlacementThatFitsSurvives` | 4096 tổ hợp. Đây là test bắt lỗi `value_shift`. |
| `RoundTrip.NeighbouringBitsAreLeftAlone` | Ghi signal này không phá signal kia. |
| `RoundTrip.InsertRejectsAValue...` | So sánh mảng trước/sau → chứng minh tính nguyên tử. |
| `Geometry.*` | Đường biên. `start=63, len=16, Motorola` là ca đặc biệt. |
| `SignExtend.ForgettingTheSignIsTheClassicBug` | Ghi lại **cả hai** cách đọc để so sánh — tài liệu sống. |
| `Dlc.*` | Bảng DLC. `RoundTripsForEveryEncodableLength` duyệt cả 16 mã. |
| `Encode.Rejects*` | Mọi đường từ chối. Đây là phần fault injection ở mức unit test. |

**Nguyên tắc đặt tên:** tên test là một **câu khẳng định**, không phải mô tả hành
động. `TheSameSignalReadUnsignedGivesNonsense` cho bạn biết ngay điều gì hỏng khi
nó fail. So với `TestDecode2` thì khác biệt rất lớn lúc 2 giờ sáng.

**`.value()` thay vì `*`:** nếu optional rỗng, `*` là UB (âm thầm), `.value()`
ném `std::bad_optional_access` mà gtest bắt và báo lỗi sạch sẽ. Trong test, fail
rõ ràng quan trọng hơn tốc độ.

---

# 7. CMake

```cmake
target_include_directories(av_can PUBLIC include)
target_link_libraries(av_can PRIVATE av_warnings)
```

**`PUBLIC include`**: ai link `av_can` cũng tự động có `include/` trong đường tìm
header. Đó là lý do `main.cpp` viết được `#include "av/can/signal.hpp"` mà
`apps/spine/CMakeLists.txt` không cần khai báo gì.

**`PRIVATE av_warnings`**: cờ cảnh báo chỉ áp cho việc build `av_can`, **không
lan sang** người dùng. Nếu để `PUBLIC`, mọi target link `av_can` cũng bị
`-Werror -Wconversion` — bạn sẽ áp tiêu chuẩn của mình lên code của người khác.
Quy tắc: `PUBLIC` cho thứ nằm trong interface (header path, thư viện mà header
cần), `PRIVATE` cho thứ chỉ ảnh hưởng quá trình build của chính mình.

Cấu trúc `include/av/can/bit.hpp` (không phải `include/bit.hpp`) là để `#include`
luôn có tiền tố namespace: `"av/can/bit.hpp"`. Tránh đụng tên với `<bit>` của
chuẩn và nói rõ header thuộc về ai.

---

# 8. Việc nên tự làm

Sửa điểm gợn ở mục 2.5 — tách `kMaxBufferBytes` khỏi `kMaxSignalBits` trong
[bit.hpp](../libs/av_can/include/av/can/bit.hpp). Nó nhỏ, an toàn, và bắt bạn
đọc lại `geometry_is_sane` đủ kỹ để hiểu tại sao dòng đó tồn tại.
