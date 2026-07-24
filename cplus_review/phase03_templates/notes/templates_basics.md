# W21 — Function/Class Template · Deduction · CTAD · Specialization

> Mục **6** để trống — tự viết.

---

## 0. Bằng chứng

```
6/6 test xanh — debug/asan/ubsan × C++20 và C++23 (ALL GREEN)
```
Nhưng bằng chứng giá trị nhất tuần này là **4 lỗi build đầu tiên**, không phải test xanh.
Xem mục 5.

---

## 1. Ba luật type deduction (thuộc 3 luật = thuộc 80%)

```cpp
int x = 42;  const int cx = x;  const int& rx = cx;
```

| Dạng tham số | `f(cx)` → `T` = | Quy tắc |
|---|---|---|
| `f(T param)` | `int` | **vứt** ref, **vứt** const/volatile |
| `f(T& param)` | `const int` | vứt ref, **giữ** const |
| `f(T&& param)` | `const int&` | universal reference → reference collapsing (W6) |

> **By-value thì const của nguồn không quan trọng — bạn không chạm vào nguồn, bạn có bản
> sao riêng.**

`std::decay_t<X>` = mô phỏng đúng cái by-value làm với type system: vứt ref, vứt cv,
mảng→con trỏ, hàm→con trỏ hàm.

## 2. CTAD (C++17) — và vì sao nó không đủ

```cpp
Box b{"hello"};      // T = const char*, KHÔNG phải std::string
```
`"hello"` là `const char[6]`. Ctor nhận **by-value** → mảng **decay** → `T = const char*`.

Bẫy production:
```cpp
Box b{std::string("hi").c_str()};   // temporary chết cuối dòng → b.value dangling 💥
```

**Deduction guide** — chỉ dẫn cho compiler, đặt ở namespace scope, **không có thân hàm**:
```cpp
SafeBox(const char*) -> SafeBox<std::string>;
```
Đây chính là thứ `std::string` cài sẵn, giải thích vì sao `std::string s = "x"` an toàn
còn `Box b{"x"}` thì không.

## 3. ⭐ Specialization KHÔNG tham gia overload resolution

```cpp
template <typename T> const char* pick(T)   { return "generic template"; }
template <>           const char* pick(int) { return "specialization"; }   // ← chết yểu
                      const char* pick(int) { return "plain function"; }

pick(42);    // → "plain function"      (KHÔNG phải "specialization")
pick(4.2);   // → "generic template"
```

### 3.1 Ba loại, đừng trộn lẫn

| Viết | Nó là gì |
|---|---|
| `template <typename T> void f(T)` | **KHUÔN ĐÚC**. Chưa có hàm nào tồn tại. |
| `void f(int)` | **MỘT HÀM THẬT**, độc lập, có danh tính riêng. |
| `template <> void f(int)` | **RUỘT THAY THẾ** dán vào một khuôn cụ thể. Không tự đứng được. |

Bằng chứng "không tự đứng được":
```cpp
void p(char*) { }                       // ✅ hợp lệ dù KHÔNG có template p nào
template <> void p<char*>(char*) { }    // ❌ lỗi nếu không có template p nào
```
Cái tên `p<char*>` nghĩa là *"cái p dạng khuôn nào đó, với T = char\*"* — nó **buộc phải
trỏ về một khuôn**. Thứ không tự đứng được thì không thể là ứng viên trong một cuộc thi.

### 3.2 Hai bước — bước ① không hề nhắc tới specialization

```
① CHỌN — ứng viên CHỈ gồm: hàm thường  +  PRIMARY template
          hòa → hàm thường thắng;  giữa các khuôn → khuôn ĐẶC THÙ HƠN thắng
          ← specialization VÔ HÌNH ở bước này
② LẤY — chỉ khi kẻ thắng là một KHUÔN, mới hỏi: khuôn đó có bản mài tay
          cho tham số này không?
```

Ẩn dụ: khuôn = **máy cắt chìa**, hàm thường = **chìa treo sẵn trên tường**,
specialization = **chìa mài tay cất trong NGĂN KÉO CỦA MỘT MÁY CỤ THỂ**. Nhân viên chọn
giữa tường và các máy; chỉ khi đã chọn máy #1 mới mở ngăn kéo máy #1.

### 3.3 ⭐ Luật, và ba khả năng của nó

> **Specialization chạy ⟺ khuôn chủ của nó thắng ở bước ①.**

| Gọi | Vào đấu trường ① | Thắng ① | Ngăn kéo của kẻ thắng | Ra |
|---|---|---|---|---|
| `f(42)` | `f(T)` + **hàm thường** `f(int)` | hàm thường | — (bản mài tay ở ngăn `f(T)`, kẻ **thua**) | `plain function` |
| `p(s)` `char* s` | `p(T)` + `p(T*)` | `p(T*)` đặc thù hơn | trống (bản mài tay ở ngăn `p(T)`, kẻ **thua**) | `p(T*)` |
| `q(42)` | **chỉ** `q(T)` | `q(T)` — thắng vì không ai đấu | có ✅ | **`SPECIALIZATION`** |

Hai câu trả lời sai phổ biến, cả hai đều là thái cực:
*"specialization luôn thắng vì khớp chính xác nhất"* ❌ — nó không thi đấu.
*"specialization không bao giờ chạy"* ❌ — nó chạy khi khuôn chủ thắng.

Cứ hỏi ①② theo đúng thứ tự thì không bao giờ sai, kể cả với code chưa từng thấy.

### 3.4 Hệ quả chí mạng — **đổi thứ tự khai báo = đổi hành vi**

Hai nhóm dưới đây **giống hệt nhau từng ký tự**, chỉ khác vị trí một dòng:

```cpp
template <typename T> void g(T)    { }
template <>           void g(int*) { }   // ← đứng TRƯỚC
template <typename T> void g(T*)   { }

template <typename T> void h(T)    { }
template <typename T> void h(T*)   { }
template <>           void h(int*) { }   // ← đứng SAU
```
```
g(p)  ->  g(T*) chạy            (specialization câm)
h(p)  ->  SPECIALIZATION chạy
```
*(đã chạy thật, GCC 13, `-std=c++23`)*

Vì sao: `template <> void g(int*)` **không nói** nó thuộc khuôn nào — compiler tự đoán,
và chỉ nhìn **những khuôn đã khai báo tính đến dòng đó**.
- Ở `g`: mới chỉ thấy `g(T)` → dán vào `g(T)`, T = `int*`
- Ở `h`: đã thấy cả `h(T*)` → khớp sát hơn → dán vào `h(T*)`, T = `int`

Rồi bước ① chạy y hệt ở cả hai (`(T*)` đặc thù hơn `(T)` → thắng). Ở `g`, bản mài tay nằm
trong ngăn kéo kẻ thua; ở `h`, nằm trong ngăn kéo kẻ thắng.

> **Bạn không điều khiển được specialization của mình dán vào đâu.** Ai đó thêm một
> overload phía trên nó — file khác, sáu tháng sau — và code bạn đổi hành vi, **không một
> warning nào**.

### 3.5 Làm gì thay vào

```cpp
template <typename T> void p(T)     { }
template <typename T> void p(T*)    { }
                      void p(char*) { }   // ← muốn xử lý riêng char*? HÀM THƯỜNG
```
Hàm thường có danh tính riêng → luôn vào thẳng bước ①, thắng ổn định, **không phụ thuộc
thứ tự khai báo**.

> **Function template: đừng specialize — dùng overload.** (Herb Sutter)
> **Class template: cứ specialize thoải mái.**

Vì sao class an toàn? **Class không có overloading.** Chỉ đúng một cái tên `std::hash` →
không có danh sách ứng viên, không có bước ①, không có "khuôn nào thắng". Ngăn kéo *luôn*
được mở. Đó cũng là lý do class template **có** partial specialization còn function
template **bị chuẩn cấm** — function đã có overload để làm việc đó.

Và đây không phải chuyện học thuật — nó là cơ chế bạn sẽ **dùng thật**:
```cpp
template <typename T> struct is_pointer     { static constexpr bool value = false; };
template <typename T> struct is_pointer<T*> { static constexpr bool value = true;  };  // → W23
template <> struct std::hash<MyType> { ... };   // dạy unordered_map băm kiểu của mình
```

## 4. Lazy instantiation — class template là CÔNG THỨC, không phải class

```cpp
template <typename T> struct Widget {
    int  ok()  const { return 1; }
    void bad() const { T::this_does_not_exist(); }   // int không có hàm này
};
Widget<int> w;  w.ok();     // ✅ BIÊN DỊCH BÌNH THƯỜNG
```
Member function chỉ được **sinh ra khi được gọi**. Thân `bad()` chưa từng được kiểm tra
ngữ nghĩa với `T = int`. Thêm `w.bad();` → build đỏ ngay.

```cpp
struct NoCompare { int x; };            // không có operator<
std::vector<NoCompare> v;
v.push_back({1});                       // ✅ hợp lệ
std::sort(v.begin(), v.end());          // ❌ lỗi 200 dòng — CHỈ ở đây
```

> **Đây là lý do STL dùng được với gần như mọi kiểu:** container không đòi kiểu của bạn
> làm được *mọi thứ*, chỉ đòi đúng những gì bạn *thực sự gọi*.
>
> **Giá phải trả:** lỗi nổ ở *chỗ dùng*, không phải chỗ *định nghĩa* → compiler phải in
> cả chuỗi instantiation → thông báo lỗi kinh hoàng. **W24 (Concepts) sinh ra để chữa
> đúng chuyện này.**

## 5. ⭐ Bốn lỗi build đầu tiên — mỗi lỗi một bài học

### 5.1 Macro không biết template là gì
```
error: macro "EXPECT_TRUE" passed 2 arguments, but takes just 1
```
```cpp
EXPECT_TRUE(std::is_same_v<int, std::decay_t<decltype(cx)>>);
//                             ↑ preprocessor thấy dấu phẩy → "2 tham số"
```
Preprocessor chạy **trước** compiler; nó chỉ đếm dấu phẩy. **Sửa: bọc thêm một cặp ngoặc.**
`static_assert` không cần — nó là **keyword thật**, không phải macro.

> Luật: **có dấu phẩy trong template bên trong macro → thêm `(...)`.**

### 5.2 Deduction guide không phải hàm
```
error: 'SafeBox(const char*) -> SafeBox<std::string>' declared 'static' but never defined
```
Trong anonymous namespace mọi thứ có internal linkage (GCC coi như `static`), nhưng guide
**không có thân** → "static mà không định nghĩa". Trực giác "guide là một hàm" **sai** —
nó chỉ là chỉ dẫn cho bước suy diễn, nằm ngoài mô hình linkage bình thường.
**Sửa: đưa ra namespace CÓ TÊN.**

### 5.3 GCC tự chứng minh mục 3
```
error: 'const char* pick(T) [with T = int]' defined but not used
```
Compiler nói bằng chính lời nó: **specialization đó là dead code.** Không cần tin lời giải
thích ở mục 3 nữa — `-Wunused-function` vừa xác nhận. Giữ lại trong code với
`[[maybe_unused]]` = *"tôi biết nó chết, đó là chủ đích"*.

> `-Werror` (Week 0) trả về giá trị đúng ở đây: nó biến một bài học lý thuyết thành
> một build đỏ mà bạn buộc phải hiểu.

### 5.4 Lỗi thứ cấp
`unused variable 'rx'` chỉ xuất hiện vì dòng dùng `rx` đã hỏng ở 5.1. **Sửa gốc, đừng sửa
triệu chứng** — đọc lỗi từ trên xuống.

## 6. Vì sao `f(T&&)` là "universal reference" mà `f(T&)` thì không — **TỰ VIẾT**

Gợi ý: khi truyền lvalue `cx` vào `f(T&&)`, `T` được suy ra là gì? Rồi `T&&` trở thành gì
sau reference collapsing (W6)? Còn truyền rvalue `42` thì `T` là gì? Vì sao **một** dạng
tham số lại bắt được **cả** lvalue lẫn rvalue, trong khi `T&` chỉ bắt được lvalue?
Điều kiện nào bắt buộc để `T&&` là universal reference chứ không phải rvalue ref thường?

**Trả lời:**




---

## 7. Câu hỏi phỏng vấn tiếp theo

- `template <typename T>` vs `template <class T>` — khác gì? (không gì cả; `typename` rõ
  nghĩa hơn vì `T` có thể là `int`)
- Vì sao **không** partial-specialize được function template? (chuẩn cấm — dùng overload)
- CTAD có hoạt động với `std::vector v = {1,2,3}` không? Với `std::vector v(3, 5)`?
  (có; và cái thứ hai cho `{5,5,5}` chứ không phải `{3,5}` — bẫy initializer_list, W2)
- `explicit` deduction guide dùng khi nào?
- Two-phase lookup là gì? Vì sao phải viết `typename T::iterator` và `this->member`
  trong class template dẫn xuất? (→ W22)
- Template code phải nằm trong header — vì sao? Có cách nào tách không?
  (explicit instantiation, `extern template`)
