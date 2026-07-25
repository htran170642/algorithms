# W24 — SFINAE · `enable_if` · Concepts

> Artifact: [`include/w24/constraints.hpp`](../include/w24/constraints.hpp) · Test: [`tests/constraints_test.cpp`](../tests/constraints_test.cpp)
> Mục **8** để trống — tự viết.

---

## 0. Bằng chứng

```
ALL GREEN — debug/asan/ubsan × C++20 và C++23
```
Nhưng bằng chứng giá trị nhất tuần này là **một lỗi build tôi cố tình giữ lại** (mục 5):
`requires{}` ở namespace scope với kiểu cụ thể là **lỗi cứng**, không phải `false`.
Cả file trả lời **một câu**: "làm sao nói 'hàm này chỉ nhận kiểu số nguyên' và bắt compiler ép?" —
rồi xem cú pháp co lại và thông báo lỗi dễ đọc dần khi đi từ C++11 sang C++20.

---

## 1. SFINAE là gì — và ranh giới "immediate context"

**SFINAE** = *Substitution Failure Is Not An Error*. Khi compiler **thay** tham số template suy ra
vào **chữ ký** hàm mà tạo ra kiểu vô nghĩa, nó **không báo lỗi** — chỉ lặng lẽ bỏ overload đó ra
khỏi tập ứng viên, rồi tìm cái khác.

Điều kiện sống còn: lỗi phải nằm trong **immediate context** — phần chữ ký đang được thay
(kiểu trả về, kiểu tham số, mệnh đề template). Lỗi trong **thân hàm** thì SFINAE không với tới.

```cpp
template <typename T> typename T::type f(T);   // CHỮ KÝ: T=int -> substitution fail -> mềm (bỏ f)
template <typename T> void g(T){ typename T::type x; }  // THÂN: T=int -> LỖI CỨNG
```

Cùng một dòng `typename T::type`, **vị trí** quyết số phận. Đây là lý do mọi điều kiện của
`enable_if` đều nhét vào **chữ ký** — để rơi vào immediate context và biến thành công cụ chọn overload.
(Chỗ quiz Q3 tôi trượt: tưởng `int::type` luôn là lỗi cứng. Không — ở chữ ký nó mềm.)

---

## 2. ⭐ Cùng một điều kiện, bốn thời đại

```cpp
// 1. tag dispatch (C++98)     — overload + kiểu-thẻ, cồng kềnh (để bảo tàng)
// 2. enable_if   (C++11)      — điều kiện giấu trong chữ ký, lỗi kinh dị
// 3. if constexpr (C++17)     — rẽ nhánh trong 1 hàm, KHÔNG điều khiển overload
// 4. concepts    (C++20)      — nói thẳng ý định, lỗi đọc được, subsumption chọn overload
```

**enable_if** — điều kiện nằm ở kiểu trả về, và hai overload **bắt buộc loại trừ nhau bằng tay**:
```cpp
template <typename T> std::enable_if_t< std::is_integral_v<T>, std::string> describe(T);
template <typename T> std::enable_if_t<!std::is_integral_v<T>, std::string> describe(T);
```
Quên dấu `!` → không phải thông báo hữu ích, mà là redefinition/ambiguity.

**if constexpr** — sạch hơn, nhưng là **công cụ khác**: một hàm rẽ nhánh lúc biên dịch. Nó **không**
tự loại khỏi overload set, **không** nói được "tôi không nhận kiểu này". Chọn nhánh, không gác cửa.

**concepts** — vị từ có tên trên kiểu. Cái hay `enable_if` không làm được: fallback là template
**không ràng buộc**, mà `Integral T` vẫn thắng nhờ **subsumption** (chuyên biệt hơn thì được chọn) —
khỏi viết `!Integral`, khỏi bookkeeping loại trừ:
```cpp
template <typename T>  std::string describe_concept(T){ return "not integral"; }  // fallback trần
template <Integral T>  std::string describe_concept(T){ return "integral"; }      // thắng cho int
```

---

## 3. `requires`-expression — hỏi "cú pháp này có hợp lệ không?"

```cpp
template <typename T>
concept Printable = requires(std::ostream& os, const T& t) {
    { os << t } -> std::same_as<std::ostream&>;
};
```
Đây là thứ `enable_if` chật vật diễn đạt: một yêu cầu về **cách dùng** (`os << t` phải biên dịch được),
không chỉ là kiểm trait. Kiểu không có `operator<<` bị từ chối ngay tại call site với
"constraint not satisfied: Printable<T>", không phải 200 dòng đổ ruột từ trong thư viện stream.

---

## 4. Bốn cú pháp, một ràng buộc (câu hỏi "có thực sự biết C++20" hay bị hỏi nhất)

```cpp
template <Integral T>              T id_a(T v);              // (a) constrained type param — gọn nhất
template <typename T> requires Integral<T>  T id_b(T v);    // (b) requires-clause đầu
template <typename T> T id_c(T v) requires Integral<T>;     // (c) requires-clause đuôi
Integral auto id_d(Integral auto v);                        // (d) abbreviated / constrained auto
```
Cả bốn nói cùng một điều. Thang co dần: `enable_if` → requires-clause → constrained type param →
`auto` param. Concept **thay thẳng** `typename` — điều `enable_if` không bao giờ làm được.
(Quiz Q5: cả A/B/C đều chạy, nhưng (a) mới là "gọn nhất C++20"; tôi trượt vì chọn requires-clause.)

---

## 5. ⭐ Lỗi build giữ lại: `requires{}` chỉ "mềm" bên trong template

Test đầu tiên của tôi viết thế này ở namespace scope — và **nổ lỗi cứng**:
```cpp
static_assert(!requires(double d){ id_a(d); });     // ❌ error: no matching function for 'id_a(double&)'
```
GCC 13 chứng minh: `requires`/SFINAE **chỉ biến lỗi thành `false` khi lỗi phát sinh trong lúc
*substitution* của một template**. Ở namespace scope với kiểu cụ thể (`double`, `NoStream`), không có
template nào đang được thay — biểu thức bị kiểm **ngay lập tức**, và "no matching function" là lỗi thật.

Sửa: bọc mỗi probe thành **variable template** (tạo ngữ cảnh phụ thuộc):
```cpp
template <typename T> constexpr bool id_a_ok = requires(T v){ id_a(v); };
static_assert( id_a_ok<int> && !id_a_ok<double>);   // ✓ mềm, vì id_a_ok<double> đang được instantiate
```
Đây **đúng y** chủ đề mục 1: SFINAE/`requires` nuốt lỗi *chỉ trong substitution*. Cùng một ranh giới,
lần này lộ ra ở phía người **dùng** concept thay vì người định nghĩa.

---

## 6. Trait vs concept — không runtime, không object

Mọi thứ ở đây giải ở compile-time: concept là vị từ `constexpr bool` trên kiểu, `describe_*` chọn nhánh/
overload lúc biên dịch. File test phần lớn là `static_assert`; mấy `EXPECT` chỉ có khi một **giá trị**
thật sự chảy qua (`id_a(5)==5`, `to_string(42)=="42"`).

---

## 7. Khi nào KHÔNG dùng

- **Không** viết concept cho ràng buộc chỉ dùng một lần, tầm thường — `requires std::integral<T>` tại chỗ đủ.
- **Không** thay mọi `if constexpr` bằng overload có concept: nếu chỉ rẽ nhánh nội bộ, `if constexpr` gọn hơn.
- **Không** lạm dụng `enable_if` trong code mới; nó còn tồn tại chủ yếu để đọc code cũ và cho compiler chưa có concept.

---

## 8. TỰ VIẾT (retrieval — tự điền, không nhìn phía trên)

> Đề: **Cùng dòng `typename T::type`, đặt ở kiểu trả về của hàm thì SFINAE bỏ overload lặng lẽ,
> nhưng đặt trong thân hàm thì lỗi cứng. Vì sao?** Trả lời bằng khái niệm "immediate context" và
> "substitution", 3–4 câu. Rồi nối sang: vì sao `static_assert(!requires(double d){ f(d); })` ở
> namespace scope lại là **lỗi cứng**, mà bọc trong variable template thì thành `false`?

_(câu trả lời của bạn ở đây)_

---

## 9. Câu hỏi phỏng vấn nối tiếp

- `if constexpr` và concept-overload cùng "rẽ theo kiểu". Cho một tình huống concept làm được mà `if constexpr` không.
- `describe_concept` có fallback **không ràng buộc** vẫn không nhập nhằng cho `int`. Cơ chế tên là gì, và vì sao `enable_if` không có nó?
- Vì sao `requires(double d){ f(d); }` ở namespace scope là lỗi cứng, nhưng trong `template<class U> ... requires(U u){ f(u); }` thì không?
- `{ os << t } -> std::same_as<std::ostream&>;` — bỏ phần `-> std::same_as<...>` đi thì concept lỏng hơn thế nào?
- Concept có thay được **mọi** `enable_if` không? Nghĩ về ràng buộc trên **giá trị non-type** hay quan hệ giữa nhiều tham số.
- Hai overload, một `requires A`, một `requires A && B`. Gọi với kiểu thoả cả hai — cái nào thắng, và "subsumption" quyết định ra sao?
