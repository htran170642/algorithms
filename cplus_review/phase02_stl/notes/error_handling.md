# W17 — optional · variant · expected · Xử lý lỗi

> Mục **5** để trống — tự viết.
> "Don't reinvent the wheel": tuần này DÙNG type chuẩn, không clone.

---

## 0. Bằng chứng (dual-standard matrix)

```
C++23: 4 test  (optional + variant×2 + expected)   ← expected có
C++20: 3 test  (optional + variant×2)              ← expected tự loại, KHÔNG lỗi build
```
Cùng một file, C++23 chạy đủ, C++20 bỏ qua tính năng chưa có. Đúng mục đích matrix.

---

## 1. `std::optional<T>` — "có thể không có"

```cpp
std::optional<int> find(key);   // có → chứa int; không → nullopt
if (auto v = find(k)) use(*v);  // convert bool, *v lấy giá trị
r.value_or(-1);                 // default nếu rỗng
```
Giải quyết: trước dùng "giá trị đặc biệt" (-1, nullptr, "") để báo không có — nhưng -1 có
thể **hợp lệ**. optional tách bạch "có" vs "không", không mượn giá trị thật làm cờ.
(HashMap/AVL `find` đã trả optional.)

## 2. `std::variant<A,B,C>` — "một trong nhiều kiểu"

```cpp
std::variant<int, std::string, double> v = 42;   // giờ là int
v = "hello";                                       // giờ là string
std::visit([](auto&& x){ ... }, v);                // gọi đúng nhánh theo kiểu hiện tại
std::holds_alternative<int>(v);  std::get<int>(v);  v.index();
```
**Type-safe union** — thay `union` kiểu C (không biết đang chứa gì). Là **đa hình đóng**
(closed): tập kiểu biết trước → tốt hơn virtual (không vptr, không heap, dispatch compile
— W7). `std::get` sai kiểu → ném `bad_variant_access`.

## 3. `std::expected<T,E>` — "giá trị HOẶC lỗi kèm lý do" (C++23)

```cpp
std::expected<int, std::string> parseAge(const std::string& s) {
    if (bad(s)) return std::unexpected("not a number: " + s);
    return age;                     // thành công → giá trị
}
if (r) use(*r); else log(r.error());   // .error() cho LÝ DO, không chỉ "thất bại"
```
optional nói "có/không". expected nói "có giá trị / **có lỗi + lý do**". Trả lỗi mà không
dùng exception.

## 4. Ba cách xử lý lỗi

| | Exceptions | Error codes | expected |
|---|---|---|---|
| chi phí khi KHÔNG lỗi | ~0 (zero-cost) | check mỗi lần | check mỗi lần |
| chi phí KHI lỗi | **đắt** (unwind) | rẻ | rẻ |
| quên xử lý | tự lan lên | **dễ quên** | `[[nodiscard]]` cảnh báo |
| hợp với | lỗi *hiếm*, nghiêm trọng | code C cũ | lỗi *thường*, dự kiến được |

> **Nguyên tắc TẦN SUẤT:** lỗi *hiếm* (hết RAM, config hỏng lúc khởi động) → **exception**.
> Lỗi *thường xảy ra* (parse fail, key vắng, timeout) → **expected**. Đừng dùng exception
> cho luồng điều khiển bình thường (đắt khi ném).

## 5. Vì sao "giá trị đặc biệt" (-1, nullptr) là anti-pattern — **TỰ WRITE**

Gợi ý: optional/expected giải quyết vấn đề gì của `-1`/`nullptr`? Khi nào -1 "trông hợp lệ"
gây bug? Vì sao `[[nodiscard]]` trên expected quan trọng?

**Trả lời:**




---

## 6. BÀI HỌC PHASE 10: bug feature-detection trong Week 0

Feature detection Week 0 viết SAI:
```cmake
check_include_file_cxx("expected" CR_HAS_EXPECTED)   # kiểm FILE tồn tại
```
File `<expected>` **có** trong libstdc++ 13 cả ở C++20, nhưng `std::expected` bên trong bị
khoá sau `#if __cplusplus > 202002L`. → `check_include_file` (chạy 1 lần lúc configure)
báo có → define vô điều kiện → build C++20 thử dùng std::expected → **lỗi**.

**Sửa đúng:** dùng feature-test macro của chuẩn:
```cpp
#include <version>
#if __cpp_lib_expected >= 202202L    // tôn trọng -std THẬT của từng lần biên dịch
```

> **Kiểm TÍNH NĂNG, không kiểm FILE.** `__cpp_lib_*` (trong `<version>`) là công cụ chuẩn.
> CMake `check_include_file` chạy một lần với một chuẩn → sai với dual-standard.

**Điều đáng nhất:** chỉ build C++23 (99% project) thì bug ẩn mãi. Vì build CẢ HAI chuẩn
mà nó lộ ra — đúng lý do plan chọn dual-standard matrix từ đầu.

---

## 7. Câu hỏi phỏng vấn tiếp theo

- `std::variant` lưu ở đâu (heap hay inline)? `sizeof(variant<int,string>)` = ? (inline,
  = max thành viên + tag)
- `std::any` khác `variant` gì? Khi nào dùng any? (type xoá hoàn toàn, cần `any_cast`)
- `std::visit` cài thế nào để O(1)? (jump table sinh lúc compile)
- Vì sao `optional<T&>` (optional của reference) **không** hợp lệ trong chuẩn? Cách thay?
- Monadic optional (C++23): `and_then`, `transform`, `or_else` — chúng giải quyết cái
  "if (opt) { ... }" lồng nhau thế nào?
