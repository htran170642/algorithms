# W18 — StringView + span · Type không sở hữu

> Mục **5** để trống — tự viết.

---

## 0. Bằng chứng — ASAN bắt dangling (W9 tái xuất)

```cpp
cr::StringView getName() {
    std::string name = "temporary name";
    return name;              // view trỏ vào LOCAL → dangling
}
cr::StringView sv = getName();
sv[0];                        // stack-use-after-return
```
```
ERROR: stack-use-after-return
  #0 StringView::operator[]  string_view.hpp:32   ← đọc buffer đã chết
  #1 main                    sv_dangle.cpp:12
```
Compiler thường KHÔNG cảnh báo. View trông vô hại nhưng chỉ là {con trỏ, độ dài} trỏ vào
thứ đã chết.

---

## 1. Vấn đề: truyền chuỗi tốn kém

```cpp
void log(const std::string& msg);
log("hello");   // "hello" là const char* → TẠO std::string tạm → cấp phát heap. Lãng phí.
```
Chỉ muốn *đọc* chuỗi mà phải cấp phát + copy.

## 2. `string_view` = {con trỏ, độ dài}, KHÔNG sở hữu

```cpp
void log(std::string_view msg);   // 16 byte: {const char*, size_t}
log("hello");        // KHÔNG cấp phát — trỏ thẳng vào literal
log(some_string);    // KHÔNG copy — trỏ vào buffer của string
```
Cách đúng để nhận tham số chuỗi **chỉ đọc**. Copy view = copy 16 byte, không đụng heap.

**Bằng chứng không copy:** `sv.data() == s.data()` (cùng con trỏ).

## 3. `span<T>` = string_view cho MỌI mảng liền kề

```cpp
void process(std::span<int> data);   // {int*, size_t}
std::vector v; std::array a; int raw[];
process(v); process(a); process(raw);   // cả ba, không template, không copy
```

## 4. ⚠️ Không sở hữu → dangling (nối W9)

```cpp
std::string_view sv = std::string("hi").substr(0,3);   // temporary chết cuối câu → dangling
```
Cùng bẫy W9 (lifetime extension không bắc cầu), nhưng string_view **âm thầm hơn** reference.

> **Quy tắc vàng:** string_view/span là **THAM SỐ**, không phải **member** hay **giá trị
> trả về**. Phải sống *ngắn hơn* dữ liệu chúng nhìn.

## 5. Vì sao substr của view khác std::string — **TỰ WRITE**

Gợi ý: `std::string::substr` làm gì (cấp phát? copy?) vs `StringView::substr`? Chi phí?
Cái nào dangling? Vì sao view nhanh hơn nhưng nguy hiểm hơn?

**Trả lời:**




---

## 6. Chi tiết cài đặt đáng nhớ

- **`operator<<` phải dùng `os.write(data, size)`**, không `os << data_`. View KHÔNG có
  `\0` kết thúc (nó là *một đoạn* của chuỗi lớn hơn) → in bằng con trỏ sẽ tràn tới `\0`
  tiếp theo.
- **`cLen` tự viết constexpr** thay `std::strlen` → `StringView("x")` tính size lúc compile.
- **Constructor `const char*` implicit** (cố ý) — literal tự thành view. Ngoại lệ hợp lý
  của quy tắc "1 tham số → explicit" (W2), như `std::string`.
- **`operator==` so NỘI DUNG**, không so con trỏ — hai buffer khác nhau cùng "abc" thì bằng.
- `[]` không kiểm biên (như `std::string_view`); `startsWith` cần guard
  `prefix.size > size` kẻo đọc quá biên.

## 7. Câu hỏi phỏng vấn tiếp theo

- `std::string_view::remove_prefix/suffix` — vì sao O(1)? (chỉ dời con trỏ/đổi size)
- Truyền `std::string_view` **by value** hay `const&`? Vì sao by value? (16 byte, rẻ hơn
  một indirection)
- `string_view` có `.c_str()` không? Vì sao KHÔNG? (không đảm bảo `\0` kết thúc)
- `std::span` có `const` propagation không? `span<const int>` vs `const span<int>` khác gì?
- Khi nào một hàm nên nhận `std::string` **by value** thay vì `string_view`? (khi nó cần
  *sở hữu*/lưu lại chuỗi → sink parameter)
