# W7 — Virtual Functions & Destructors

> Mục **6** để trống — tự viết.

---

## 0. Bằng chứng

### vtable thật, GCC in ra (`-fdump-lang-class`)

```
Vtable for Circle — 6 entries
 0   (int (*)(...))0          ← offset-to-top (cho multiple inheritance, W8)
 8   & _ZTI6Circle            ← con trỏ typeinfo (cho dynamic_cast, typeid)
16   Circle::~Circle          ← destructor
24   Circle::~Circle          ← destructor (bản deleting — CÓ 2 BẢN)
32   Circle::name             ← ô [0] mà lời gọi ảo nhảy tới
40   Circle::area             ← ô [1]

Class Circle  size=16 align=8
```

- **`vptr` KHÔNG trỏ vào đầu vtable** — nó trỏ vào **offset 16** (hàm đầu tiên).
  Hai ô trên nằm ở **địa chỉ âm** so với vptr → đó là cách `dynamic_cast`/`typeid` chạy.
- **Destructor có 2 bản**: một bản chỉ huỷ, một bản huỷ-rồi-`operator delete`.
- Trong vtable của Circle là **`Circle::name`**, không phải `Shape::name` → **đó chính
  là** dynamic dispatch: cùng ô [0], nội dung khác nhau tuỳ class.

### sizeof — cái giá của `virtual`

```
sizeof(Plain)   =  4      struct { int x; void f(); }
sizeof(Virtual) = 16      struct { int x; virtual void f(); }
&v.x lệch 8 byte so với &v   → vptr ngồi ĐẦU, x đứng sau
```

**8 (vptr) + 4 (int) = 12 → làm tròn lên 16** vì `alignof = 8` (con trỏ cần căn lề 8).
Thêm một chữ `virtual` → object phình **4 lần**. `vector<Virtual>` 1 triệu phần tử tốn
16 MB thay vì 4 MB, và cache miss gấp 4. *(padding/alignment → W27)*

---

## 1. Cơ chế: vptr + vtable

Class có hàm `virtual` → compiler nhét vào object một con trỏ ẩn **`vptr`**, trỏ tới
**vtable** — mảng con trỏ hàm, **mỗi class một bản** dùng chung cho mọi object.

Gọi `animal->speak()`: compiler không biết là Dog hay Cat → sinh mã *"lấy vptr → nhảy
tới ô [0] → gọi"*. Quyết định **lúc chạy** = **dynamic dispatch**.

**Giá:** +8 byte mỗi object, +1 lần dereference mỗi lời gọi, và **chặn inline**.

---

## 2. Quên `virtual ~Base()` = **UB**, không phải leak

```cpp
struct Base { ~Base(); };                  // KHÔNG virtual
Base* p = new Derived;
delete p;                                  // ← UNDEFINED BEHAVIOR
```

`delete p` chỉ thấy kiểu **tĩnh** `Base*` → gọi `~Base()` → **`~Derived()` không bao
giờ chạy**. Đo được: `BadDerived::dtor_calls == 0`.

Chuẩn nói đây là **UB**, không phải "leak" — compiler được phép làm **bất cứ gì**.

> **Quy tắc:** class dùng làm base để `delete` qua con trỏ → destructor **PHẢI**
> `virtual`. Không định cho kế thừa → đánh dấu `final`, hoặc để destructor
> `protected` non-virtual.

**Week 0 đã chặn sẵn:**
```
error: 'cr::BadBase' has virtual functions and accessible non-virtual destructor
                                                    [-Werror=non-virtual-dtor]
error: deleting object of polymorphic class type 'cr::BadBase' which has
       non-virtual destructor might cause undefined behavior
                                                    [-Werror=delete-non-virtual-dtor]
```
GCC thấy hết **không cần ai nhắc**. Project không bật cảnh báo này → biên dịch êm ru →
bug tới production.

---

## 3. Object slicing

```cpp
SigFixed derived;
SigBase& ref    = derived;   ref.speak()    == "fixed"   ✅ đa hình
SigBase  sliced = derived;   sliced.speak() == "base"    💀 bị cắt
```

`sliced` chỉ đủ chỗ chứa `SigBase`. Phần `SigFixed` **bị vứt**, và `vptr` của nó bị đặt
về vtable của **SigBase**. Đa hình **chết im lặng**.

> Đa hình **luôn** phải qua reference hoặc pointer. Không bao giờ by-value.

---

## 4. `const` là một phần của SIGNATURE

```cpp
struct SigBase   { virtual std::string speak() const; };   // speak() const
struct SigBroken : SigBase { std::string speak(); };       // speak()  ← KHÁC HÀM
```

Với compiler, `speak() const` và `speak()` là **hai hàm khác nhau**, khác y như
`speak(int)` vs `speak(double)`. Nên `SigBroken::speak()` **không override gì cả** — nó
tạo hàm mới toanh, vtable vẫn giữ `SigBase::speak() const`.

Đo được:
```
broken.speak()   → "broken"    // gọi trực tiếp: tìm thấy hàm mới
as_base.speak()  → "base"      // qua vtable: vẫn là SigBase::speak()
```

Thêm `override` → compiler nói thẳng:
```
error: 'std::string Broken::speak()' marked 'override', but does not override
```

> `override` **không đổi hành vi** — nó biến một **bug im lặng** thành **lỗi biên dịch**.
> Viết nó cho **mọi** hàm định override. Miễn phí, bắt cả một họ bug.

Ba hệ quả của `const` trên member function:

| | `speak()` | `speak() const` |
|---|---|---|
| `this` | `S*` | `const S*` |
| sửa được member? | có | **không** |
| gọi trên object `const`? | **không** | có |
| signature | `speak()` | `speak() const` — **khác** |

Hệ quả 2 là lý do CLAUDE.md đòi **const correctness**: hàm quên `const` → mọi `const&`
đều không gọi được nó → bệnh lan ra cả codebase.

---

## 5. NVI — Non-Virtual Interface

```cpp
class Report {
public:
    std::string generate() const {          // NON-virtual: hợp đồng công khai
        return "<<" + body() + ">>";        // base sở hữu phần bao ngoài
    }
private:
    virtual std::string body() const = 0;   // hook — PRIVATE virtual
};
```

Public thì **non-virtual**, customization point thì **private virtual**. Base giữ quyền
kiểm soát các bước pre/post; subclass **không thể bỏ qua** chúng (vì `generate()` không
virtual).

Đo được: `SalesReport` chỉ cấp `"sales"`, base tự thêm `<< >>` → `"<<sales>>"`.

> Pattern đúng mà **đa số codebase không dùng**. Đối lập với thói quen "public virtual"
> — thứ khiến subclass tuỳ tiện phá vỡ invariant của base.

---

## 6. Ba cái bẫy của đa hình — **TỰ VIẾT**

Tóm tắt 3 cách đa hình gãy, mỗi cái 1–2 câu, kèm cách phòng:

1. destructor:

2. truyền by-value:

3. signature lệch:

---

## 7. Câu hỏi phỏng vấn tiếp theo

- Gọi hàm `virtual` **trong constructor** thì sao? *(gợi ý: lúc đó vptr đang trỏ vào
  vtable của class nào? Đây là câu hỏi ưa thích của Bloomberg)*
- `final` giúp compiler **devirtualize** thế nào? Đo được không?
- Vì sao vtable có **2 destructor**? Khác nhau chỗ nào?
- `dynamic_cast` hoạt động ra sao — dựa vào ô nào trong vtable? Vì sao nó chậm?
- Khi nào **KHÔNG** nên dùng virtual? *(gợi ý: CRTP ở W25, `std::variant` ở W17)*
