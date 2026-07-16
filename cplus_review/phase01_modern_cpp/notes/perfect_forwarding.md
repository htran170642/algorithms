# W6 — Perfect Forwarding

> Mục **5** để trống — tự viết. Bạn đã trả lời đúng 1-B / 2-A trong buổi học.

---

## 0. Bằng chứng (tự đo — `/tmp/why.cpp`)

```
TRUOC:     original = "precious"
byMove   -> original = "<moved-from>"   ← BỊ RÚT RUỘT
byForward-> original = "precious"       ← CÒN NGUYÊN
```

Caller viết y hệt nhau — `byMove(original)` vs `byForward(original)`, cùng truyền
**lvalue**. Một cái phá biến của caller, một cái không. Caller **không có cách nào
biết trước**.

---

## 1. Vấn đề: 2ⁿ overload

Hàm bọc phải chuyển tham số sang hàm khác mà **không làm thay đổi bản chất** của nó.

- nhận **by value** → copy thừa
- nhận **`const&`** → *luôn* copy, dù caller đưa rvalue (đo được: `copies=1 moves=0`)
- viết cả 2 overload → **2ⁿ** overload cho n tham số. Không khả thi.

> **Perfect forwarding = chuyển tiếp mà GIỮ NGUYÊN value category.**
> lvalue vào → lvalue ra. rvalue vào → rvalue ra.

---

## 2. Forwarding reference ≠ rvalue reference

```cpp
void f(String&& x);          // rvalue reference — chỉ nhận rvalue
template <typename T>
void g(T&& x);               // FORWARDING reference — nhận CẢ HAI
```

Nhìn giống hệt (`&&`), khác hoàn toàn. `T&&` là forwarding reference **chỉ khi** `T`
là template parameter đang được **suy luận ngay tại đó**.

---

## 3. Reference collapsing — `T` mang theo câu trả lời

| caller truyền | `T` suy ra | `T&&` | thu gọn |
|---|---|---|---|
| **lvalue** `String s` | `String&` | `String& &&` | **`String&`** |
| **rvalue** `String{}` | `String` | `String&&` | **`String&&`** |

Quy tắc: **chỉ `&& + &&` ra `&&`. Mọi tổ hợp có `&` đều ra `&`.**

```
& &  → &      && &  → &
& && → &      && && → &&    ← chỉ trường hợp này
```

> **Thông tin lvalue/rvalue được MÃ HOÁ vào `T`.** `forward` chỉ việc đọc lại nó.

---

## 4. `move` vs `forward` — khác đúng một chỗ

```cpp
move:     static_cast<remove_reference_t<T>&&>(t)   // bóc & → LUÔN rvalue
forward:  static_cast<T&&>(t)                       // giữ T → tuỳ T mà & hoặc &&
```

Một chữ `remove_reference_t`. Toàn bộ khác biệt giữa **"cướp đi"** và **"giữ nguyên"**.

| | Cast | Có điều kiện? | Dùng khi |
|---|---|---|---|
| `move(x)` | `remove_reference_t<T>&&` | **không** | chắc chắn muốn cướp |
| `forward<T>(x)` | `T&&` | **có** (qua `T`) | chuyển tiếp, giữ nguyên |

### Vì sao tham số là `remove_reference_t<T>&` chứ không phải `T&`

Nếu viết `forward(T& t)` thì `T` **bị suy luận tại đây** từ `t` — mà `t` **có tên → là
lvalue** (W1!) → `T` luôn ra `String&` → **move không bao giờ xảy ra**. Hỏng.

`remove_reference_t<T>` là **non-deduced context** — compiler không suy ngược được
(vì `String`, `String&`, `String&&` đều cho ra `String`). Nó **buộc** phải ghi `<T>`
tường minh.

> Đó là lý do **luôn** thấy `std::forward<Args>(args)` có `<...>`, còn `std::move(x)`
> thì không. **Bắt buộc kỹ thuật**, không phải quy ước.

---

## 5. Vì sao `move` trên forwarding reference là BUG — **TỰ VIẾT**

Gợi ý (bạn đã hiểu được sau khi nhìn `/tmp/why.cpp`):
- `x` bên trong hàm có phải bản sao không? Nó trỏ vào đâu?
- `T = Probe&` nghĩa là caller đã làm gì — cho mượn hay cho luôn?
- `move` có đọc `T` không? Nó làm gì với thông tin "caller chỉ cho mượn"?
- Sau đó object của caller ra sao? Caller có biết không?

**Trả lời:**




**Quy tắc:**
> `T&&` **trong template** (forwarding reference) → **LUÔN** `std::forward<T>`,
> **KHÔNG BAO GIỜ** `std::move`.

*Ngoại lệ:* `T&&` **không** trong template (`void f(Probe&& p)`) là rvalue reference
thật — caller đã cố ý đưa rvalue → `std::move(p)` là **đúng**. Chính là bài W1: `p` có
tên nên là lvalue, phải move lần nữa.

---

## 6. `makeUnique` — lời giải cho bài toán 2ⁿ

```cpp
template <typename T, typename... Args>
std::unique_ptr<T> makeUnique(Args&&... args) {
    return std::unique_ptr<T>(new T(cr::forward<Args>(args)...));
}
```

`Args&&...` — variadic forwarding reference. `forward<Args>(args)...` bung ra thành
`forward<Arg1>(arg1), forward<Arg2>(arg2), …` — **mỗi tham số forward độc lập**: cái
nào lvalue thì copy, cái nào rvalue thì move.

Đo được: `makeUnique<pair<Probe,Probe>>(lv, Probe{"rv"})` → `copies=1` (cho `lv`),
`moves>=1` (cho temporary), và `lv` **còn nguyên**.

Một hàm. Mọi tổ hợp. Đó là lý do perfect forwarding tồn tại.

---

## 7. Câu hỏi phỏng vấn — có đáp án

### 7.1 Cái nào thật sự là forwarding reference?

Điều kiện **đủ và cần**: phải là **`T&&`** với `T` là template param **đang được suy
luận ngay tại đó**. Chỉ cần lệch một chút là hỏng:

| Viết | Là forwarding ref? | Vì sao |
|---|---|---|
| `template<class T> void f(T&& x)` | ✅ | đúng dạng, `T` suy luận tại đây |
| `auto&& x = expr;` | ✅ | `auto` suy luận **y hệt** template param |
| `template<class T> void f(const T&& x)` | ❌ | có `const` → **không** còn dạng thuần `T&&` → rvalue ref thật |
| `template<class T> void f(std::vector<T>&& v)` | ❌ | `T` suy từ *bên trong* `vector<T>`, không phải từ `T&&` |
| `template<class T> void f(T&& x)` **nhưng gọi** `f<int>(...)` | ❌ | chỉ định tường minh → không suy luận nữa |

**`T&&` trong class template — KHÔNG phải forwarding reference:**

```cpp
template <typename T>
class Vector {
    void push_back(T&& v);      // ❌ T đã CỐ ĐỊNH lúc khai báo Vector<int>
                                //    -> đây là int&&, rvalue ref thật

    template <typename U>
    void emplace_back(U&& u);   // ✅ U suy luận tại LỜI GỌI -> forwarding ref
};
```

> **Mấu chốt:** `T` của class được ấn định khi bạn viết `Vector<int>`. Tới lúc gọi
> `push_back` thì **chẳng còn gì để suy luận** — `T&&` là `int&&`, cứng. Muốn có
> forwarding reference thì **method phải có template param RIÊNG** của nó (`U`).

### 7.2 `vector<T>::push_back(T&&)` — vì sao không phải forwarding reference?

Đúng như trên: `T` thuộc về **class**, không thuộc method. Với `vector<string>`:

```cpp
void push_back(const string& v);   // overload 1 — nhận lvalue -> copy
void push_back(string&& v);        // overload 2 — nhận rvalue -> move
```

Hai overload **viết tay**, không phải forwarding. Đây chính là bài toán 2ⁿ mà
`std::vector` chấp nhận trả giá — vì `push_back` chỉ có **1 tham số** nên 2¹ = 2
overload, chịu được.

Còn `emplace_back(Args&&...)` thì **là** forwarding reference thật, vì `Args` là
template param riêng của method.

### 7.3 Vì sao `emplace_back` nhanh hơn `push_back`?

```cpp
std::vector<std::string> v;

v.push_back(std::string(10, 'x'));   // 1. dựng temporary
                                     // 2. MOVE temporary vào slot
                                     // 3. huỷ temporary
v.emplace_back(10, 'x');             // dựng THẲNG trong slot. Hết.
```

`emplace_back` forward `10` và `'x'` **nguyên vẹn** tới placement new, rồi
`::new (&data_[i]) std::string(10, 'x')` construct **ngay tại chỗ**. Không temporary,
không move, không dtor thừa.

> `push_back` = "dựng ở ngoài rồi mang vào". `emplace_back` = "dựng luôn ở trong".

**Nhưng đừng lạm dụng** — `emplace_back` **bỏ qua explicit**:
```cpp
std::vector<Timeout> v;
v.emplace_back(30);   // COMPILE! Dù Timeout(int) là explicit (W2!)
v.push_back(30);      // lỗi biên dịch — đúng như mong muốn
```
`emplace_back` dùng **direct-initialization** (`T(args...)`), mà direct-init thì
explicit constructor **vẫn được gọi**. Nó vừa nhanh hơn vừa **kém an toàn hơn**.
Đây là trade-off ít người biết.

### 7.4 `forward` hai lần trên cùng một biến?

```cpp
template <typename T>
void bad(T&& x) {
    sink1(std::forward<T>(x));   // nếu T là rvalue -> x bị RÚT RUỘT ở đây
    sink2(std::forward<T>(x));   // ...rồi forward CÁI XÁC sang sink2
}
```

**Bug.** Với rvalue, `forward` lần đầu cho phép `sink1` cướp nội dung của `x`. Lần thứ
hai forward một object đã ở trạng thái *moved-from* — `sink2` nhận rác.

Không phải UB (moved-from là *valid but unspecified*), nhưng là **bug logic**, và
**không compiler nào cảnh báo**. `.clang-tidy` của Week 0 có `bugprone-use-after-move`
bắt được các ca đơn giản.

> **Quy tắc:** `forward` (và `move`) là thao tác **một lần duy nhất** trên một biến.
> Nó có nghĩa "tao xong với mày rồi".

Muốn đưa cho nhiều nơi? → forward cho **cái cuối cùng** thôi:
```cpp
sink1(x);                      // lvalue -> copy
sink2(std::forward<T>(x));     // cái cuối mới được cướp
```
