# W8 — Object Model & Layout

> Mục **5** để trống — tự viết.

---

## 0. Bằng chứng (tự đo)

### Con trỏ base thứ hai bị DỊCH
```
&c     = ...912
(A*)&c = ...912   lệch 0     ← base 1: cùng địa chỉ
(B*)&c = ...928   lệch 16    ← base 2: CON TRỎ BỊ DỊCH GIÁ TRỊ
```

### Layout thật của `C : A, B { int c; }`
```
A::vptr @0   A::a @8        (A data: byte 0..11)
B::vptr @16  B::b @24       (B data: byte 16..27)
C::c    @28  ← chui vào tail padding của B!
sizeof(C) = 32   (KHÔNG phải 40)
```

---

## 1. Multiple inheritance → nhiều vptr

Class kế thừa 2 base có virtual → object có **2 vptr**, mỗi base một vùng riêng.

```cpp
C c;
A* pa = &c;    // == &c        (offset 0)
B* pb = &c;    // == &c + 16   ← con trỏ ĐỔI GIÁ TRỊ
```

**`static_cast` sang base thứ hai làm con trỏ THAY ĐỔI GIÁ TRỊ**, để trỏ đúng vào phần
base đó bên trong object.

> Đây là lý do **KHÔNG BAO GIỜ** `reinterpret_cast` giữa các kiểu có quan hệ kế thừa —
> nó không cộng offset. Chỉ `static_cast`/`dynamic_cast` biết điều chỉnh.

Cái ô `offset-to-top` bí ẩn trong vtable dump (W7) là để `delete pb` tìm lại con trỏ
gốc đầu object mà `operator delete`.

---

## 2. ⚠️ Tail padding reuse — `sizeof(class)` ≠ tổng `sizeof(base)`

**Chính người dạy cũng đoán sai chỗ này** (đoán 40, thật ra 32). Nó phản trực giác:

Một type có **HAI** kích thước:
- **`sizeof`** (size) — khi là object độc lập, **có** tail padding
- **data size** (nvsize) — phần dữ liệu thật, **không** tail padding

Khi làm **base subobject**, compiler dùng *data size*, và member của lớp dẫn xuất được
**nhồi vào tail padding của base**:

```
sizeof(A) = 16   (12 data + 4 tail pad, khi độc lập)
nhưng trong C, A chỉ chiếm 12 byte data; C::c chui vào phần padding
→ sizeof(C) = 32, không phải 16+16+... = 40
```

> **`sizeof` của một class KHÔNG bằng tổng `sizeof` các base.** Bẫy kinh điển.

---

## 3. Bài toán kim cương

```cpp
struct Animal { int age; };
struct Dog : Animal {};
struct Cat : Animal {};
struct Chimera : Dog, Cat {};    // HAI bản Animal
// chimera.age → LỖI: ambiguous
```

## 4. Virtual inheritance — lời giải & cái giá

```cpp
struct Dog : virtual Animal {};
struct Cat : virtual Animal {};
struct Chimera : Dog, Cat {};    // MỘT Animal dùng chung
```

`virtual` base = "dù bị kế thừa qua bao nhiêu đường, chỉ giữ MỘT bản".

**Cái giá:** `Dog`/`Cat` compile *trước khi* biết sẽ bị gộp trong `Chimera`, nên không
biết `Animal` nằm ở offset nào. `Animal` bị đẩy xuống **cuối** object, truy cập qua một
**vbase offset** trong vtable → thêm một lần dereference. Đo được:
`sizeof(ChimeraV) > sizeof(ChimeraPlain)`.

> `std::iostream` là kim cương thật: `istream` và `ostream` cùng virtual-inherit từ
> `ios_base`. Ví dụ kinh điển nhất.

---

## 5. Empty Base Optimization — **TỰ VIẾT**

Đo được:
```
sizeof(Empty)          = 1    (object rỗng độc lập vẫn cần địa chỉ)
sizeof(HasEmptyMember) = 8    (Empty là MEMBER: 1 byte + padding)
sizeof(DerivesEmpty)   = 4    (Empty là BASE: BIẾN MẤT)
```

Giải thích: vì sao object rỗng cần `sizeof >= 1`? Vì sao khi làm **base** thì quy tắc đó
được miễn? EBO liên quan gì tới `sizeof(unique_ptr<T>) == sizeof(T*)`?

**Trả lời:**




---

## 6. Câu hỏi phỏng vấn tiếp theo

- `[[no_unique_address]]` (C++20) — nó làm EBO hoạt động cho **member** thế nào? Đo
  `sizeof(HasEmptyMember)` với attribute này.
- Vì sao `static_cast` xuống (downcast) không cần RTTI mà `dynamic_cast` thì cần?
- Trong đa kế thừa, `delete` một `B*` (base thứ hai) — làm sao runtime tìm lại đầu
  object để `operator delete`? *(gợi ý: offset-to-top)*
- `reinterpret_cast<B*>(&c)` thay vì `static_cast` — hỏng thế nào, và khi nào crash?
- Vì sao thứ tự khai báo member ảnh hưởng `sizeof`? (→ W27, struct packing)
