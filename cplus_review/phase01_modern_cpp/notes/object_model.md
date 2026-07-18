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

## 6. Câu hỏi phỏng vấn — có đáp án

### 6.1 `[[no_unique_address]]` (C++20) — EBO cho member

EBO chỉ tự động áp dụng cho **base**, không cho **member**. Attribute này mở khoá EBO
cho member. Đo được:

```
struct Plain     {                          Empty e; int x; };  → sizeof 8
struct Optimized { [[no_unique_address]]    Empty e; int x; };  → sizeof 4
```

`e` không còn chiếm byte riêng — nó **chồng lên** cùng địa chỉ với `x` (vì nó rỗng,
không có gì để đè lên nhau). Đây là cách hiện đại để có EBO **mà không cần** trick kế
thừa base rỗng.

> Ứng dụng thật: `std::vector<T, Alloc>` với allocator rỗng (mặc định), hoặc lambda
> làm comparator trong `std::map` — không phình object thêm byte nào.

### 6.2 Vì sao downcast `static_cast` không cần RTTI, `dynamic_cast` thì cần?

- **`static_cast<Derived*>(base_ptr)`** — offset điều chỉnh là **hằng số biết lúc biên
  dịch** (compiler biết chính xác `Derived` layout ra sao). Nó chỉ cộng/trừ một số cố
  định. **Không kiểm tra gì lúc chạy** — nếu con trỏ thật ra không phải `Derived`, đó
  là **UB im lặng**, không ai báo.

- **`dynamic_cast<Derived*>(base_ptr)`** — phải **kiểm tra lúc chạy** xem object thật
  có đúng là `Derived` không, vì nó có thể là bất kỳ lớp con nào. Kiểm tra đó cần đọc
  `type_info` từ vtable → cần **RTTI**. Trả `nullptr`/ném nếu sai.

> `static_cast` = "tao TIN mày đúng kiểu, cộng offset là xong". `dynamic_cast` = "để
> tao KIỂM TRA đã". Nhanh vs an toàn.

Hệ quả: `dynamic_cast` **chỉ chạy được trên polymorphic type** (có ít nhất 1 virtual).
`static_cast` chạy trên mọi type.

### 6.3 `delete` một `B*` (base thứ hai) — runtime tìm lại đầu object thế nào?

Nhớ W8: `(B*)&c` **lệch 16 byte** so với đầu object. Nhưng `operator delete` cần con
trỏ **đầu object thật** (chỗ `malloc`/`new` trả về) — không phải con trỏ đã dịch.

Cơ chế: `~B()` là **virtual**, nên `delete pb` đi qua vtable của B. Trong vtable đó có
ô **`offset-to-top`** = "-16" — lùi 16 byte để về đầu object. Runtime:
1. gọi deleting destructor qua vtable
2. đọc `offset-to-top`, cộng vào `pb` → ra con trỏ gốc
3. `operator delete(con-trỏ-gốc)`

> Đây chính là công dụng của cái ô `offset-to-top` bí ẩn trong vtable dump ở W7.
> **Và** là lý do `delete` qua base pointer **bắt buộc** destructor phải virtual trong
> đa kế thừa — không có nó, runtime không tìm lại được đầu object → free sai địa chỉ.

### 6.4 `reinterpret_cast<B*>(&c)` — hỏng thế nào?

```cpp
C c;
B* good = static_cast<B*>(&c);        // = &c + 16  (đúng)
B* bad  = reinterpret_cast<B*>(&c);   // = &c       (SAI — không cộng offset)
```

`reinterpret_cast` **chỉ đổi cách nhìn con trỏ, không đổi giá trị**. Nên `bad` trỏ vào
đầu object (phần A), rồi bị *diễn giải* như thể đó là một `B`.

- `bad->fb()` → đọc vptr ở đầu object = **vptr của A** → gọi nhầm hàm, hoặc nhảy vào
  địa chỉ rác → **crash hoặc còn tệ hơn**.
- `bad->b` → đọc `A::a` (hoặc padding) tưởng là `B::b` → dữ liệu sai im lặng.

> Với single inheritance offset thường = 0 nên `reinterpret_cast` "may mà chạy" —
> chính điều đó khiến người ta tưởng nó an toàn, rồi chết khi chuyển sang đa kế thừa.
> **Không bao giờ `reinterpret_cast` giữa các kiểu có quan hệ kế thừa.**

### 6.5 Vì sao thứ tự member ảnh hưởng `sizeof`?

```cpp
struct Bad  { char a; int b; char c; };   // sizeof 12
struct Good { int b; char a; char c; };   // sizeof 8
```

Mỗi type có yêu cầu **alignment**: `int` phải nằm ở địa chỉ chia hết cho 4. `Bad` xếp
`char a`(1) rồi phải **độn 3 byte** để `int b` căn lề, rồi `char c`(1) + độn 3 → 12.
`Good` gom `int` lên đầu, hai `char` liền nhau ở cuối → chỉ độn 2 → 8.

> **Xếp member từ LỚN tới NHỎ** thường cho object nhỏ nhất. Chi tiết đầy đủ ở **W27**
> (struct packing, cache locality) — đây là bản xem trước.
