# Mock Interview #1 — Modern C++ (Phase 1)

**Ngày:** kết thúc Phase 1 (W1–W10)
**Chế độ:** teaching-through (hỏi → bí thì giảng)
**Điểm:** ~5.8/10 trên các trục đã học
**Phát hiện chính:** mạnh ở đọc-code-tìm-bug, yếu ở tái tạo khái niệm trừu tượng dưới áp lực.

> Ôn lại: đọc câu hỏi, **tự trả lời trước**, rồi mới xem đáp án. Đừng đọc xuôi.

---

## Câu 1 — NRVO (warmup) · *đã phải xem đáp án*

Đồng nghiệp bảo `return v;` chậm, nên sửa `return std::move(v);`. Đúng hay sai?

```cpp
std::vector<int> makeData() {
    std::vector<int> v;
    v.push_back(1);
    return v;              // vs  return std::move(v);
}
```

**Đáp án:**
- **SAI** — `std::move` chặn **NRVO**.
- **NRVO cho con số:** 1 construct, **0 copy, 0 move**. `v` được dựng thẳng vào ô nhớ
  của `result` — chúng là **cùng một object**. NRVO không "giảm" move, nó **xoá sạch**
  object trung gian.
- **Vì sao `std::move` chặn NRVO:** NRVO cần biểu thức return là **tên của biến local**
  (`return v;`). `std::move(v)` = `static_cast<vector&&>(v)` → biến nó thành **xvalue**,
  không còn là "tên v" → điều kiện NRVO không thoả → buộc gọi move ctor.
- **Tệ hơn bao nhiêu:** so NRVO (0 thao tác), `return std::move(v)` thêm **1 move + 1
  destructor**. **KHÔNG có copy** (vector có move ctor noexcept). Bằng output W1:
  `copies=0, moves=1`.

---

## Câu 2 — Destructor giết move (W3+W5) · *đã phải xem đáp án*

```cpp
class Logger {
    Logger(std::string n) : name_(std::move(n)) {}
    ~Logger() { flush(); }              // ← thủ phạm
    std::string name_;
    std::vector<char> buffer_;
};
std::vector<Logger> loggers;            // realloc → COPY thay vì MOVE
```

**Đáp án — chuỗi 3 bước:**
1. **`~Logger()` giết move ctor** — khai báo destructor → compiler ngừng sinh move
   (W3). `Logger` thành copy-only.
2. **vector cần move noexcept để dám move** — `move_if_noexcept` hỏi "có move noexcept
   không?", không có → rơi về **copy** (W1+W5).
3. **copy Logger đắt** — sao chép cả `string` + `vector<char>`, có cấp phát heap.

**Đo được** (`logger_demo.cpp`): `LoggerBad` → copies=6 moves=0; `LoggerGood` →
copies=0 moves=6.

**Bonus bug:** copy-only còn khiến `~Logger()` chạy mỗi lần realloc → `flush()` bị gọi
thừa hàng trăm lần → vừa chậm vừa **sai logic**.

**Sửa — Rule of Zero:** bỏ `~Logger()`, đưa flush vào member RAII:
```cpp
class FlushOnDestroy {
    std::vector<char>& buf_;
public:
    explicit FlushOnDestroy(std::vector<char>& b) : buf_(b) {}
    ~FlushOnDestroy() { writeToFile(buf_); }   // flush nằm đây
};
class Logger {
    std::string       name_;
    std::vector<char> buffer_;
    FlushOnDestroy    flusher_{buffer_};   // ← khai báo + khởi tạo. flush tự chạy khi huỷ.
};
```
⚠️ **Thứ tự khai báo:** `buffer_` phải TRƯỚC `flusher_` → lúc huỷ `flusher_` chết trước
(huỷ ngược thứ tự) nên `buffer_` còn sống khi flush. Đảo lại = UB.

---

## Câu 3 — const là part of signature (W7) · *TỰ TRẢ LỜI ĐÚNG ✅*

```cpp
struct Shape  { virtual double area()       { return 0.0; } };   // non-const
struct Circle { double area() const { ... } };                   // const → in ra 0!
```

**Đáp án:** `const` là **một phần của signature**. `area()` và `area() const` là hai
hàm khác nhau → `Circle::area() const` **không override** `Shape::area()` → vtable của
Circle vẫn giữ `Shape::area` → gọi qua `Shape&` ra `0`.

`override` không sửa bug — nó **biến bug im lặng thành lỗi biên dịch**
("does not override"). Viết `override` cho mọi hàm định override.

---

## Câu 4 — Object slicing (W7) · *đã phải xem đáp án*

```cpp
std::vector<Monster> army;        // ← BUG Ở ĐÂY: chứa Monster BY VALUE
army.push_back(Dragon{});         // Dragon bị CẮT thành Monster
army[0].attack();                 // → 1, không phải 100
```

**Đáp án:** **object slicing**. `vector<Monster>` lưu object cỡ `sizeof(Monster)`;
`Dragon` không nhét vừa → copy ctor của Monster chỉ chép phần Monster, phần Dragon bị
vứt, vptr reset về vtable Monster → đa hình chết.

> **Cờ đỏ nhận ra ngay:** `std::vector<Base>` (base by value) + Base có `virtual` =
> gần như chắc chắn slicing. Đa hình + lưu by value = mâu thuẫn.

**Sửa — lưu con trỏ:**
```cpp
std::vector<std::unique_ptr<Monster>> army;
army.push_back(std::make_unique<Dragon>());
army[0]->attack();   // → 100 ✅
```
Đa hình **chỉ** hoạt động qua pointer/reference, không bao giờ by value.

---

## Scorecard

| Trục | Điểm | Ghi chú |
|---|---|---|
| Modern C++ | 6/10 | nhận ra tốt, tái tạo chi tiết yếu |
| STL | 5/10 | vector move/realloc chưa vững số |
| Memory | 6/10 | RAII, thứ tự huỷ — nắm sau ví dụ |
| Performance | 5/10 | hiểu copy đắt, chưa định lượng |
| Communication | 7/10 | **câu hỏi làm rõ rất tốt — điểm mạnh thật** |

## Việc cần làm trước Phase 2
- [ ] Viết nốt các mục note "TỰ VIẾT" (W1–W10) — chữa khoảng cách "nhận ra → tái tạo"
- [ ] Đừng "trả lời giúp tôi" quá nhanh — ép đoán thêm 60s, thiếu *niềm tin* chứ không thiếu kiến thức
- [ ] Ôn bằng **code chạy được**, không bằng định nghĩa (đúng điểm mạnh)
- [ ] Làm lại Mock #1 sau, chế độ nghiêm túc, không xem đáp án
