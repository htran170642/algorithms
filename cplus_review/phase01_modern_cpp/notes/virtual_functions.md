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

## 7. Câu hỏi phỏng vấn — có đáp án

### 7.1 Gọi hàm `virtual` trong constructor? *(câu ruột của Bloomberg)*

**Đa hình KHÔNG hoạt động.** Đo được:

```
Derived d;
  Base()    goi speak() -> "Base"                 ← KHONG phai "Derived"!
  Derived() goi speak() -> "Derived-da-san-sang"
sau khi dung xong: d.speak() -> "Derived-da-san-sang"
```

**Vì sao:** object được dựng **từ base lên**. Khi `Base()` đang chạy, phần `Derived`
**chưa tồn tại** — `msg_` chưa được khởi tạo. Nếu `speak()` gọi được `Derived::speak()`
thì nó sẽ đọc `msg_` khi biến này còn là **rác**.

Nên chuẩn C++ quy định: **`vptr` được cập nhật theo từng tầng.** Vào `Base()` thì vptr
trỏ vào **vtable của Base**; `Base()` xong, vptr được trỏ lại **vtable của Derived**
rồi `Derived()` mới chạy.

> Trong constructor/destructor của X, kiểu động của object **CHÍNH LÀ X** — không phải
> class dẫn xuất. Đa hình bị "tắt" có chủ đích, để bảo vệ bạn khỏi đọc member chưa
> khởi tạo.

**Destructor thì ngược lại** — huỷ từ dẫn xuất xuống base, nên trong `~Base()` thì
`Derived` **đã bị huỷ rồi** → vptr lại trỏ về Base. Cùng một lý do.

**Nguy hiểm hơn:** gọi hàm **pure virtual** trong constructor →
`pure virtual method called` → `std::terminate()`. Chương trình chết.

**Cách làm đúng:** hàm khởi tạo hai pha (`create()` gọi ctor rồi gọi `init()`), hoặc
truyền dữ liệu qua tham số ctor thay vì gọi hook ảo.

### 7.2 `final` giúp devirtualize thế nào?

`final` nói với compiler: **"không còn class nào override nữa."**

```cpp
struct Circle final : Shape { double area() const override; };

Circle c;
c.area();          // compiler BIẾT chắc là Circle::area -> gọi thẳng, có thể INLINE
Shape* p = &c;
p->area();         // vẫn phải qua vtable... TRỪ KHI compiler chứng minh được p là Circle
```

Không có `final`, khi thấy `Shape* p` compiler **buộc** phải qua vtable — biết đâu có
`Square` nào đó. Có `final` trên `Circle`, nếu compiler suy ra được kiểu động là
`Circle` thì nó **bỏ hẳn** lời gọi gián tiếp và **inline** thẳng.

**Đo được:** so asm ở `-O2` với/không `final` trên Godbolt, hoặc benchmark
(→ **W25**, khi so CRTP với virtual dispatch).

Lợi ích thật không nằm ở việc bỏ 1 lần dereference — mà ở chỗ **inline mở đường cho
mọi tối ưu khác**. Virtual call là một **hàng rào chặn tối ưu**.

### 7.3 Vì sao vtable có 2 destructor?

```
16   Circle::~Circle      ← complete object destructor  (D1)
24   Circle::~Circle      ← deleting destructor         (D0)
```

- **D1 (complete)**: chỉ huỷ member + base. Dùng khi object trên **stack** ra khỏi
  scope, hoặc khi nó là member của object khác.
- **D0 (deleting)**: huỷ **rồi gọi `operator delete`** để trả bộ nhớ. Dùng cho
  `delete p`.

Cần cả hai vì `delete p` phải giải phóng bộ nhớ, còn object trên stack thì **không được
phép** — bộ nhớ đó không phải của heap. Compiler không biết trước bạn sẽ dùng cách nào,
nên nó sinh cả hai và để vtable chọn lúc chạy.

*(Chi tiết Itanium ABI: còn có D2 — base object destructor — dùng cho virtual
inheritance. → W8)*

### 7.4 `dynamic_cast` hoạt động ra sao? Vì sao chậm?

Nhớ vtable dump chứ:
```
 0   (int (*)(...))0          ← offset-to-top
 8   & _ZTI6Circle            ← con trỏ TYPEINFO   ← dynamic_cast dùng ô này
16   Circle::~Circle          ← vptr trỏ vào ĐÂY
```

`vptr` trỏ vào offset 16, nên **typeinfo nằm ở `vptr[-1]`** và **offset-to-top ở
`vptr[-2]`** — địa chỉ **âm**.

`dynamic_cast<Derived*>(base_ptr)` làm:
1. lấy `vptr` từ object
2. đọc `vptr[-1]` → con trỏ `std::type_info`
3. **duyệt cây kế thừa lúc chạy**, so sánh tên kiểu (strcmp!) để tìm đường
4. nếu tìm thấy → cộng offset để chỉnh con trỏ; không → trả `nullptr`

**Vì sao chậm:** bước 3 là một **thuật toán duyệt đồ thị**, không phải một phép so
sánh. Với đa kế thừa nó phải dò nhiều nhánh. libstdc++ còn dùng `strcmp` trên tên kiểu
khi qua ranh giới shared library. Chậm hơn virtual call **hàng chục lần**.

> `dynamic_cast` nhiều trong hot path = code smell. Thường nghĩa là bạn **đang thiếu
> một hàm virtual** — hoặc nên dùng `std::variant` + `visit` (W17).

### 7.5 Khi nào KHÔNG dùng virtual?

| Tình huống | Dùng gì thay thế |
|---|---|
| Tập kiểu **đóng**, biết trước hết | **`std::variant` + `visit`** (W17) — dispatch lúc biên dịch, không vptr |
| Cần hiệu năng, đa hình **tĩnh** | **CRTP** (W25) — inline được hoàn toàn |
| Chỉ cần một hành vi thay đổi | **`std::function`** hoặc lambda — đơn giản hơn cả cây kế thừa |
| Object nhỏ, số lượng cực lớn | **không dùng gì** — +8 byte/object và cache miss giết bạn |
| Chỉ để "cho linh hoạt sau này" | **YAGNI** — virtual là chi phí *có thật*, linh hoạt là lợi ích *giả định* |

**Chi phí thật của virtual:**
1. **+8 byte** mỗi object (đo được: 4 → 16)
2. **+1 dereference** mỗi lời gọi
3. **chặn inline** ← đắt nhất, vì nó chặn *mọi* tối ưu khác
4. **phá cache** — vtable là một lần chạm bộ nhớ nữa
5. buộc phải cấp phát heap (`unique_ptr<Base>`) thay vì để trên stack

> Dùng virtual khi bạn **thật sự cần** tập kiểu **mở** (plugin, kiểu do người dùng thêm
> vào). Còn tập kiểu **đóng** thì `variant` gần như luôn nhanh hơn và an toàn hơn.
