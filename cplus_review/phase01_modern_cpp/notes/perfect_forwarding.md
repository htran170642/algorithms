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

## 7. Câu hỏi phỏng vấn tiếp theo

- `auto&& x = ...;` có phải forwarding reference không? Còn `const T&&`? Còn `T&&` khi
  `T` là template param của **class** (không phải hàm)?
- `vector<T>::push_back(T&&)` — có phải forwarding reference không? *(Không! Vì sao?)*
- Vì sao `emplace_back` nhanh hơn `push_back`? (dùng `Probe` để đo — W5 đã có sẵn)
- `std::forward` gọi **hai lần** trên cùng một biến thì sao? *(bug: cướp hai lần)*
