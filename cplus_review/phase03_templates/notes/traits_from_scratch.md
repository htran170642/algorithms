# W23 — Tự xây `<type_traits>` từ số 0

> Artifact: [`include/w23/traits.hpp`](../include/w23/traits.hpp) · Test: [`tests/traits_test.cpp`](../tests/traits_test.cpp)
> Mục **8** để trống — tự viết.

---

## 0. Bằng chứng

```
ALL GREEN — debug/asan/ubsan × C++20 và C++23
```
~40 `static_assert`: trait được kiểm ở **compile-time**, nên *compile được nghĩa là pass*.
Cả file chỉ là **partial specialization** — đúng bộ máy của `Tuple` ở W22, chĩa sang việc khác.
Ở W22 nó tách pack thành head/tail để **lưu** dữ liệu. Ở đây cùng khuôn "primary template +
specialization" ấy lại dùng để **hỏi câu hỏi về một kiểu**. Hiểu `Tuple<Head, Tail...>` là đã
hiểu mọi trait dưới đây; chỉ cần nhận ra mỗi cái thuộc **họ nào trong hai họ**.

---

## 1. ⭐ Điều cả tuần xoay quanh: **hai họ trait**

| | Hỏi gì | Trả lời là | Thành viên | Đọc bằng |
|---|---|---|---|---|
| **Value trait** | "T có phải …?" | một `bool` | `static constexpr bool value` | `Trait<T>::value` |
| **Type trait**  | "biến T thành …" | một **kiểu** | `using type = …;` | `typename Trait<T>::type` |

`is_same`, `is_pointer`, `is_integral`, `is_const` → họ **value** (`::value`).
`remove_reference`, `remove_cv`, `conditional`, `enable_if`, `decay` → họ **type** (`::type`).

**Chỗ tôi trượt (Q3):** tôi trả lời `static constexpr auto type = int;` cho `remove_reference`.
Nó **không biên dịch nổi** — vế phải dấu `=` cần một **biểu thức**, mà `int` là một **kiểu**,
không phải biểu thức. Không thể nhét một kiểu vào một biến. Struct mang một kiểu ra ngoài chỉ
bằng **member typedef** (`using type = …;`), không cách nào khác. Lẫn hai họ là lỗi kinh điển
của người mới — và là lý do cái bảng này nằm trên cùng.

---

## 2. Cái hộp chở giá trị: `integral_constant`

```cpp
template <typename T, T v>
struct integral_constant {
    static constexpr T value = v;
    using value_type = T;
    using type = integral_constant;
    constexpr operator value_type() const noexcept { return value; }
    constexpr value_type operator()() const noexcept { return value; }
};
using true_type  = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;
```

Sao phải bày ra, thay vì viết thẳng `static constexpr bool value` trong mỗi trait? Vì
`true_type`/`false_type` là **kiểu**, nên một value trait chỉ cần **kế thừa** một trong hai là có
`value` miễn phí:

```cpp
template <typename T> struct is_pointer_helper     : false_type { };  // kế thừa value=false
template <typename T> struct is_pointer_helper<T*> : true_type  { };  // kế thừa value=true
```

Không phải tự tay viết `value` ở đâu cả — đó là toàn bộ giá trị của câu 5 trong quiz.

---

## 3. Value trait — khớp mẫu trên một kiểu

```cpp
template <typename T, typename U> struct is_same       : false_type { };
template <typename T>             struct is_same<T, T>  : true_type  { };
```

Bản `<T, T>` chỉ khớp khi hai tham số là **cùng một kiểu**. Không có object nào của `T` được tạo
ra — đó là đáp án câu 4: nó so sánh *kiểu*, nên không thể là hàm `bool` nhận giá trị, và nó chạy
được cả với kiểu không bao giờ tạo được object (`void`, lớp abstract, kiểu hàm).

`is_pointer` **bóc cv trước** để `int* const` vẫn tính là con trỏ:

```cpp
template <typename T> struct is_pointer : is_pointer_helper<remove_cv_t<T>> { };
```

`is_integral` không có mẫu nào để tận dụng, nên xây bằng cách **ghép** một trait đã tin cậy —
`is_same` chạy qua danh sách hữu hạn các kiểu số nguyên. Trait ghép được với nhau; đó là điểm mấu chốt.

---

## 4. Type trait — chở một kiểu ra ngoài

```cpp
template <typename T> struct remove_reference      { using type = T; };
template <typename T> struct remove_reference<T&>  { using type = T; };
template <typename T> struct remove_reference<T&&> { using type = T; };
```

Hai mẫu độc lập (`T&`, `T&&`) vì lvalue ref và rvalue ref là hai hình dạng kiểu khác nhau.
`remove_cv` **không cần specialization riêng** — chỉ nối remover của const với volatile:

```cpp
template <typename T> struct remove_cv { using type = remove_volatile_t<remove_const_t<T>>; };
```

Để ý ranh giới đã làm test suýt sai: `remove_cv_t<const int*>` là `const int*`, **không đổi** —
kiểu ở tầng ngoài cùng là *con trỏ*, con trỏ này không có cv; chữ `const` nằm trên **cái được trỏ**.
Nhưng `remove_cv_t<int* const>` **là** `int*`: ở đây `const` nằm trên chính con trỏ.

---

## 5. ⭐ Sự bất đối xứng sẽ nở thành SFINAE (cầu sang W24)

```cpp
template <bool B, typename T = void> struct enable_if { };            // KHÔNG có ::type
template <typename T> struct enable_if<true, T> { using type = T; };  // chỉ đây mới có ::type
```

`enable_if_t<true, X>` là `X`. `enable_if_t<false, X>` gọi tên `::type` trên một struct **không có
thành viên `type`** → ill-formed. Bây giờ đó là lỗi cứng. Tuần sau (SFINAE) đúng cái "thiếu `::type`"
này thôi lỗi và trở thành **lặng lẽ loại khỏi overload set** — đó là toàn bộ mánh của `enable_if`,
và là lý do primary được cố ý để rỗng.

`conditional` là người anh em trầm tính hơn — một `?:` ở compile-time, luôn cho ra một kiểu:

```cpp
template <bool B, typename T, typename F> struct conditional            { using type = T; };
template <typename T, typename F>         struct conditional<false,T,F> { using type = F; };
```

---

## 6. `decay` — nơi những trait nhỏ đền đáp

`decay<T>` = "một tham số truyền **theo giá trị** thì kiểu biến thành gì?" Nó **không có gì mới**,
chỉ là ghép:

```cpp
template <typename T>
class decay {
    using U = remove_reference_t<T>;                       // 1. bỏ reference
public:
    using type = conditional_t<is_array_v<U>,    remove_extent_t<U>*,      // mảng -> con trỏ
                 conditional_t<is_function_v<U>,  add_pointer_t<U>,        // hàm  -> con trỏ
                                                  remove_cv_t<U>>>;        // còn lại -> bóc cv
};
```

`decay_t<const int&>` → `int`; `decay_t<int[5]>` → `int*`; `decay_t<int(int)>` → `int(*)(int)`.
Sáu trait tí hon (remove_reference, is_array, remove_extent, is_function, add_pointer, remove_cv)
nối bằng hai `conditional_t`. Đây chính là dáng của code `<type_traits>` thật.

`is_function` dùng một sự thật khôn ngoan: kiểu hàm là kiểu **duy nhất** không nhận được cả `const`
lẫn `&`, nên `!is_const_v<const T> && !is_lvalue_reference_v<T>` phát hiện được nó.

---

## 7. Không `sizeof`, không object, không runtime

Không có gì ở đây tạo object hay chạy lúc runtime. Mọi đáp án là một kiểu hoặc một `constexpr` bool
được giải trong lúc biên dịch — nên file test là một bức tường `static_assert`, và cái `TEST` duy nhất
chỉ tồn tại để GoogleTest có thứ đăng ký. Compile được là pass.

---

## 8. TỰ VIẾT (retrieval — tự điền, không nhìn lại phía trên)

> Đề: **Vì sao `is_same<T, U>` chạy được trên cả những kiểu không bao giờ tạo được object
> (`void`, lớp abstract, kiểu hàm), trong khi một hàm giả định `bool is_same(T, U)` thì không?**
> Trả lời 3–4 câu, chỉ rõ bản hàm sẽ **đòi hỏi** điều gì mà bản trait không bao giờ cần.

_(câu trả lời của bạn ở đây)_

---

## 9. Câu hỏi phỏng vấn nối tiếp

- Vì sao `is_pointer` phải bóc cv **trước** khi khớp `T*`? Cho một kiểu mà bỏ bước đó sẽ ra sai.
- Primary của `enable_if` cố ý để rỗng. Tuần sau hỏng chuyện gì nếu nó lại khai `using type = void;`?
- `remove_cv_t<const int*>` là `const int*`, không phải `int*`. Giải thích theo **vị trí** của `const`.
- Vì sao `decay` là `class` với `using U` **private**, không phải `struct` thường? (Để public thì lộ ra cái gì?)
- `is_function` nhận diện "kiểu duy nhất không chịu cả const lẫn reference". Kiểu nào **khác** cũng nổi tiếng vì từ chối `const` — và vì sao nó không lừa được trait này?
- `<type_traits>` hiện đại đánh dấu các trait này bằng intrinsic của compiler (`__is_same`, `__is_pointer`). Vì sao nhà cung cấp thư viện chuẩn lại **bỏ qua** chính kỹ thuật ta vừa xây?
