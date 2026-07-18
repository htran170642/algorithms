# W10 Phần A — constexpr / consteval / constinit + Concepts

> Mục **5** để trống — tự viết.

---

## 0. Bằng chứng

`static_assert` **chỉ nhìn được giá trị compile-time**. Mỗi dòng dưới compile được =
compiler đã tự tính, không phải CPU chạy lúc runtime:

```cpp
static_assert(factorial(5) == 120);        // tính lúc BIÊN DỊCH
static_assert(makeSquares()[9] == 81);     // cả lookup table
static_assert(kindOf<int>() == "integral");
static_assert(Number<int>);
static_assert(!Number<std::string_view>);
```

Lỗi concept **đọc được** (lý do #1 concepts tồn tại):
```
error: no matching function for call to 'addNumbers(const char[2], const char[2])'
note: constraints not satisfied
note: 'Number<T>' [with T = const char*]
note: no operand of the disjunction is satisfied
   concept Number = std::integral<T> || std::floating_point<T>;
```
Thời chưa có concepts: cùng lỗi này = 50-100 dòng rác sâu trong `operator+`.

---

## 1. `constexpr` — "CÓ THỂ chạy lúc biên dịch"

```cpp
constexpr int factorial(int n);

static_assert(factorial(5) == 120);   // hằng → tính lúc compile
int n = readInput();
factorial(n);                          // runtime → chạy lúc runtime, vẫn OK
```

**"Có thể", không phải "bắt buộc".** Cùng hàm: hằng thì compile-time, biến thì runtime.
Lợi ích: kết quả nằm sẵn trong binary → 0 chi phí runtime (lookup table, hằng số...).

## 2. `consteval` — "BẮT BUỘC lúc biên dịch" (C++20)

```cpp
consteval int forced(int n);
forced(5);        // OK
forced(n);        // LỖI BIÊN DỊCH — n không phải hằng, không có đường lùi
```

Trắc nghiệm đã trả lời đúng (B):
```cpp
constexpr int f(int); consteval int g(int);
int n = runtime();
f(n);   // OK   — constexpr rơi về runtime
g(n);   // LỖI — consteval cấm runtime
```

> `constexpr` = "được phép chạy lúc compile". `consteval` = "cấm chạy lúc runtime".

Dùng khi muốn **đảm bảo** không bao giờ chạy runtime: kiểm tra format string, sinh ID
lúc compile.

## 3. `constinit` — khởi tạo compile-time, nhưng vẫn sửa được

```cpp
constinit int counter = 0;   // ép init lúc compile, SAU đó sửa runtime được
```

Giải quyết **static initialization order fiasco**: global ở file này phụ thuộc global
ở file kia, thứ tự init không xác định → bug. `constinit` ép giá trị đầu là hằng
compile-time → hết phụ thuộc thứ tự.

Khác `constexpr`: `constexpr` = hằng, bất biến. `constinit` = init compile-time nhưng
**mutable**.

## 4. Concepts (C++20) — hợp đồng cho template

```cpp
template <typename T>
concept Number = std::integral<T> || std::floating_point<T>;

template <Number T>              // ràng buộc tại đây
constexpr T addNumbers(T a, T b) { return a + b; }
```

**Ba lý do tồn tại:**
1. **Thông báo lỗi đọc được** — "không thoả Number" thay vì 50 dòng rác ✅ đã thấy
2. **Kiểm soát overload** — chọn hàm theo *tính chất* của type
3. **Tài liệu tự thân** — `template <Number T>` nói rõ nó cần gì

## 5. `if constexpr` — rẽ nhánh lúc biên dịch

```cpp
if constexpr (std::is_integral_v<T>) { ... }   // nhánh SAI bị VỨT, không compile
else                                 { ... }
```

Khác `if` thường: nhánh không được chọn **không hề được biên dịch** — nên có thể viết
code chỉ hợp lệ cho một loại T trong mỗi nhánh. Đây là công cụ thay cho SFINAE (W24).

---

## 6. Vì sao đẩy tính toán sang compile-time — **TỰ VIẾT**

Gợi ý: lợi gì về hiệu năng runtime? `static_assert` chứng minh điều gì? Khi nào
**KHÔNG** nên constexpr hoá (gợi ý: thời gian biên dịch, code phức tạp)?

**Trả lời:**




---

## 7. Câu hỏi phỏng vấn tiếp theo

- `constexpr` hàm có **bắt buộc** chạy lúc compile không? Làm sao *ép* nó? (gợi ý:
  gán cho biến `constexpr`, hoặc dùng trong `static_assert`)
- C++20 cho phép `constexpr` cấp phát heap (`new`/`delete`) và dùng `std::vector`
  trong constexpr — với điều kiện gì? *(phải free trước khi hết compile-time)*
- Concept vs `enable_if` (SFINAE) — khác gì? Vì sao concept thắng? (→ W24)
- `consteval` function gọi `constexpr` function được không? Ngược lại? *(một chiều)*
- Vì sao `std::is_integral_v<T>` (type trait) là bước đệm dẫn tới concepts? (→ W23)
