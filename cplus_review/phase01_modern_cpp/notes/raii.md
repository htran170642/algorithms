# W3 — RAII · Rule of 0/3/5 · Exception Safety

> Mục **3** và **7** để trống — bạn đã tự trả lời được chúng trong buổi học.
> Viết lại bằng lời bạn.

---

## 0. Bằng chứng (`tests/raii_test.cpp`, 6/6 PASS)

```cpp
// Constructor ném → destructor KHÔNG chạy, nhưng member đã xong thì VẪN được huỷ
EXPECT_FALSE(HalfBuilt::dtor_ran);        // object chưa từng tồn tại
EXPECT_EQ(Probe::counters().dtor, 2);     // a_ và b_ vẫn được dọn

// Một destructor RỖNG giết chết move semantics
struct RuleOfZero { Probe p_; };              // → move_ctor == 1
struct HasDtor    { Probe p_; ~HasDtor(){} }; // → copy_ctor == 1  ← COPY!
```

---

## 1. RAII

> **Vòng đời tài nguyên = vòng đời object.** Constructor giành. Destructor trả.

Sức mạnh nằm ở chỗ: destructor **được bảo đảm chạy** khi ra khỏi scope — kể cả
`return` sớm, kể cả **exception ném xuyên qua**. Java phải dùng `try/finally`,
Go phải dùng `defer`; C++ làm điều đó bằng **kiểu dữ liệu**.

```cpp
void f() {
    std::lock_guard lock(m_);   // giành
    mayThrow();                 // ném? không sao
    return;                     // return sớm? không sao
}                               // ~lock_guard() LUÔN chạy. Không thể quên.
```

---

## 2. Constructor ném → destructor KHÔNG chạy

> Destructor chỉ chạy cho object **đã construct XONG**.
> Constructor ném giữa chừng ⇒ object **chưa từng tồn tại** ⇒ không có gì để huỷ.

**Nhưng** các **member đã construct xong vẫn được huỷ**, theo **thứ tự ngược**.

```cpp
class HalfBuilt {
    Probe a_;   // 1. OK
    Probe b_;   // 2. OK
    Boom  c_;   // 3. NÉM
    ~HalfBuilt();   // ← KHÔNG BAO GIỜ CHẠY
};
// → ~b_(), ~a_() chạy.  ~HalfBuilt() không.
```

### Hệ quả sống còn

```cpp
Leaky() {
    buf_ = new char[1024];   // tài nguyên THÔ trong thân ctor
    mayThrow();              // ném → ~Leaky() không chạy → RÒ RỈ VĨNH VIỄN
}
```

`buf_` là con trỏ thô — **không phải object có destructor** — nên không ai dọn nó.

> **Mọi tài nguyên phải nằm trong một member kiểu RAII.**
> Vì **member thì được huỷ**; destructor của class thì **không**.

Đây là lý do sâu xa RAII không phải "phong cách đẹp" — nó là thứ **duy nhất đúng**
khi có exception.

---

## 3. Vì sao `ScopedFile` phải move-only?

**Vì `FILE*` có đúng MỘT chủ sở hữu.**

Nếu cho phép copy, hai object sẽ cùng giữ **một con trỏ**, và cả hai đều *tin rằng*
mình có trách nhiệm `fclose()` nó. Khi cả hai ra khỏi scope:

> **`fclose()` hai lần trên cùng một `FILE*` → double free → undefined behavior.**

Thường là crash. Đôi khi tệ hơn: file descriptor đó đã được OS **cấp lại cho người
khác**, và bạn vừa đóng file của họ — một bug gần như không thể lần ra.

Nên:
```cpp
ScopedFile(const ScopedFile&)            = delete;   // chặn ở compile time
ScopedFile& operator=(const ScopedFile&) = delete;

ScopedFile(ScopedFile&& other) noexcept
    : f_(std::exchange(other.f_, nullptr)) {}        // chuyển quyền: chủ cũ mất quyền
```

`std::exchange` bỏ lại `nullptr` cho `other` → `other` không còn là chủ → destructor
của nó thấy `nullptr` và **không đóng gì cả**. Không thể double-close.

> **Move-only = độc quyền sở hữu.**

Cùng mô hình này: `unique_ptr`, `std::thread`, `std::fstream`, `lock_guard`.
Sẽ tự viết lại ở **W19**.

---

## 4. Rule of 0 / 3 / 5

- **Rule of 3** (C++98): cần **dtor**, **copy ctor**, hay **copy assign** → cần **cả ba**.
  Vì nhu cầu đó nghĩa là class **quản lý tài nguyên**, mà mặc định compiler chỉ copy
  *cạn* → double-free.
- **Rule of 5** (C++11): thêm **move ctor** + **move assign**.
- **Rule of 0**: **đừng khai báo cái nào.** Để `string`/`vector`/`unique_ptr` tự lo.

## 5. ⚠️ Cái bẫy — destructor giết move

```cpp
class Logger {
    ~Logger() { flush(); }      // một destructor "vô hại"
    std::vector<int> data_;
};
```

> **Khai báo destructor ⇒ compiler NGỪNG sinh move ctor và move assign.**

Class im lặng thành **copy-only**. `std::vector<Logger>` realloc → **copy** toàn bộ.
Không warning. Không lỗi. Chỉ chậm gấp hàng chục lần.

**Vì sao chuẩn làm thế:** *"Mày tự viết destructor ⇒ chắc class mày quản lý tài nguyên
đặc biệt ⇒ tao không dám đoán cách move ⇒ thà không sinh còn hơn sinh sai."*
Bảo thủ **có chủ đích** — nhưng cái giá rất đắt.

**Hai cách sửa:**
1. Khai báo đủ Rule of 5 (`= default` cho 4 cái còn lại), **hoặc**
2. **Bỏ luôn destructor** → Rule of Zero. Thường là hướng đúng: `flush()` nên nằm
   trong một member RAII, không nằm trong `~Logger()`.

> Rule of Zero tồn tại **không phải** để gõ ít chữ hơn. Nó tồn tại vì **chạm vào một
> hàm đặc biệt sẽ âm thầm tắt những hàm khác.**

*(`.clang-tidy` Week 0: `cppcoreguidelines-special-member-functions` — mức lỗi.)*

---

## 6. Ba mức exception safety

| Mức | Cam kết | Ví dụ |
|---|---|---|
| **nothrow** | **Không bao giờ ném** | destructor, `swap`, move ctor |
| **strong** | Ném thì **như chưa từng gọi**. Toàn bộ hoặc không gì cả. | `vector::push_back` |
| **basic** | Ném thì object vẫn **hợp lệ**, nhưng trạng thái **không xác định** | phần lớn hàm |

**`std::vector` cam kết `strong` khi realloc** → khép vòng tròn với W1:
nó **không dám move** trừ khi move `noexcept`, vì move mà ném giữa chừng thì buffer cũ
đã bị rút ruột, **không còn đường lùi**. Nên nó đành **copy** — chậm, nhưng nguồn còn
nguyên, vẫn lùi được.

> `noexcept` trên move **chính là** thứ cho phép `vector` chọn đường nhanh mà vẫn giữ lời hứa.

---

## 7. Vì sao destructor không bao giờ được ném?

Bối cảnh: một exception **đang bay**, stack đang **unwind**, và C++ đang lần lượt gọi
destructor của từng object trên đường đi.

Bây giờ một destructor **ném thêm exception thứ hai**.

Giờ có **hai exception cùng lúc**. Và C++ **không có cơ chế nào để chở hai exception
song song** — một `throw` chỉ mang được một. Không thể chọn cái nào thắng. Không thể
gộp. Không thể hoãn.

Nên chuẩn chọn phương án dứt khoát nhất:

> ### `std::terminate()` — giết process ngay lập tức.

Không unwind tiếp. Không `catch`. Không cứu. **Chương trình chết.**

Vì thế từ **C++11**, destructor **mặc định là `noexcept`** — không cần viết. Và nếu cố
ném từ destructor, nó vẫn gọi `terminate()`.

Đó là lý do `ScopedFile::close()` được đánh dấu `noexcept` và **cố tình nuốt** lỗi của
`fclose()`.



**Nghịch lý của destructor:** nếu `fclose()` fail, bạn *muốn* báo lỗi — nhưng
**không được phép**. Chỉ có hai lựa chọn: **nuốt lỗi** hoặc **chết**.

Muốn báo lỗi khi đóng? → cung cấp hàm `close()` công khai để người dùng gọi tường minh
và bắt lỗi; destructor chỉ là **lưới an toàn** cuối. Đó là cách `std::fstream` làm.

---

## 8. Câu hỏi phỏng vấn tiếp theo

- Vì sao **copy-and-swap** cho strong guarantee gần như miễn phí? Viết `operator=`
  bằng copy-and-swap. *(Sẽ dùng ở W4 khi build `String`.)*
- `= default` và `{}` (thân rỗng) khác nhau thế nào? Vì sao `~Foo() = default;` **vẫn**
  giết move, giống hệt `~Foo() {}`?
- Khai báo **copy constructor** có giết move không? Còn khai báo **move ctor** có giết
  copy không?
- Vì sao `std::vector` cần **strong** guarantee, mà `std::deque::push_back` thì chỉ cần
  **basic**?
