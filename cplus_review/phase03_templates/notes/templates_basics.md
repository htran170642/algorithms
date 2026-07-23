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

Compiler chọn theo **hai giai đoạn**:
```
① OVERLOAD RESOLUTION — ứng viên: hàm thường + PRIMARY template
     hàm thường thắng template khi khớp ngang nhau   → plain function ✅
② chỉ khi template thắng ở ①, MỚI xét specialization của nó
     → không bao giờ tới đây
```
Specialization không phải "ứng viên". Nó chỉ là **cách cài đặt khác cho một template đã
thắng**. Template thua ở ① → specialization vô hình.

Hệ quả gây sốc — **đổi thứ tự khai báo = đổi hành vi**:
```cpp
template <typename T> void g(T);           // #1
template <typename T> void g(T*);          // #2 overload khác
template <>           void g<int*>(int*);  // specialize của #1
g(ptr);   // #2 thắng ① → specialization của #1 KHÔNG BAO GIỜ chạy
```

> **Đừng specialize function template. Dùng overload.** (Herb Sutter)
> Specialization dành cho **class template**, nơi nó hoạt động đúng trực giác.

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
