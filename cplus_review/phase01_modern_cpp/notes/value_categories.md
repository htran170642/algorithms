# W1 — Value Categories

> **Cảnh báo cho chính mình:** đây là note *được viết hộ*. Đọc nó thấy hiểu là chuyện
> bình thường và **không chứng minh gì cả**. Bài kiểm tra thật: một tuần nữa, đóng file
> này lại, mở file trắng, viết lại mục 4 và mục 5 bằng trí nhớ. Làm được thì mới là của bạn.

---

## 0. Bằng chứng (đã đo, không phải nghe kể)

### (A) `return local;` — NRVO
```
Probe(string)  [nrvo]
~Probe()  [nrvo]
```
→ 1 construct, 1 destruct. **Không move, không copy.**

### (B) `return std::move(local);` — NRVO bị giết
```
Probe(string)  [pessimized]
Probe(Probe&&)  <-- move          ← chi phí tự thêm vào
~Probe()  [<moved-from>]          ← xác của local, ở (A) không tồn tại
~Probe()  [pessimized]
```

### (C) `Probe&& r = std::move(a); Probe b = r;`
```
Probe(const Probe&)   <== COPY
```
→ khai báo là `&&`, nhưng vẫn **copy**.

### (D) `noexcept` trên move constructor
```
Fragile (move KHÔNG noexcept):  copies=1  moves=0
Strong  (move CÓ noexcept):     copies=0  moves=1
```

---

## 1. Hai trục phân loại

Mọi biểu thức bị hỏi **hai câu độc lập**:

1. **Danh tính (identity)** — nó có chỉ tới một object *có địa chỉ, gọi tên được*, mà tôi trỏ tới nhiều lần vẫn là cùng một thứ không?
2. **Cướp được (movable)** — tôi có được phép **rút ruột** nó không? Tức là: nó **sắp chết** chưa?

|  | **Không cướp được** | **Cướp được** *(sắp chết)* |
|---|---|---|
| **Có danh tính** | `lvalue` | `xvalue` |
| **Không danh tính** | ❌ **bất khả** | `prvalue` |

### Vì sao ô thứ tư bất khả

Không danh tính **và** không cướp được nghĩa là: tôi **không trỏ tới nó được** (không có địa chỉ, không có tên), và tôi cũng **không lấy giá trị của nó được**.

Vậy tôi làm được gì với nó? **Không gì cả.** Một biểu thức như thế không quan sát được, không dùng được — nó không tồn tại theo đúng nghĩa đen. Ngôn ngữ không cần đặt tên cho hư vô.

### Hai loại còn lại chỉ là hợp của các ô

- **glvalue** ("generalized lvalue") = *có danh tính* = `lvalue` **+** `xvalue`
- **rvalue** = *cướp được* = `xvalue` **+** `prvalue`

`xvalue` nằm ở **giao** của hai nhóm — vừa có danh tính, vừa cướp được. Đó là lý do nó tồn tại, và cũng là lý do người ta thấy rối.

---

## 2. Vì sao `"hello"` là lvalue mà `42` là prvalue?

Thử lấy địa chỉ — trình biên dịch trả lời hộ:

```cpp
&"hello";   // HỢP LỆ  -> const char (*)[6]
&42;        // LỖI     -> không có địa chỉ để mà lấy
```

`"hello"` là một `const char[6]` nằm ở **static storage**, tồn tại suốt đời chương trình, **có địa chỉ thật**. Có địa chỉ → có danh tính → **lvalue**.

`42` không phải object. Nó là **giá trị thuần**, có thể được nhét thẳng vào một thanh ghi hoặc vào chính lệnh máy, **không bao giờ chạm bộ nhớ**. Không địa chỉ → không danh tính → **prvalue**.

> **Bẫy phỏng vấn:** phần lớn ứng viên đoán `"hello"` là rvalue "vì nó là literal". Sai. Nó là **lvalue** duy nhất trong đám literal.

---

## 3. Vì sao `Probe&& r` lại sinh ra copy?

Vì **type** và **value category** là **hai thứ khác nhau**:

> Kiểu của `r` là `Probe&&`. Nhưng **biểu thức** `r` là một **lvalue**.

`r` **có tên**. Nó sống tới cuối scope. Nó **chưa sắp chết**. Compiler không có quyền rút ruột nó — biết đâu dòng sau bạn còn dùng `r`? Nên `Probe b = r;` gọi **copy constructor**.

### Quy tắc bỏ túi

> **Cái gì có tên, cái đó là lvalue.** Kể cả khi cái tên đó có kiểu `&&`.

Nói cách khác: `&&` trên **khai báo** nói *"tôi có thể **nhận** rvalue"*. Nó **không** biến bản thân biến đó thành rvalue.

### Hệ quả: vì sao trong `f(T&& param)` vẫn phải `std::move(param)`

Vì `param` **có tên** → là **lvalue**. Nếu bạn chuyển nó tiếp mà không `std::move`, nó sẽ **copy** — dù bạn đã khai báo `&&` rất cẩn thận.

```cpp
void f(Probe&& p) {
    Probe local = p;              // COPY. Ngoài dự tính.
    Probe local = std::move(p);   // move. Phải nói ra.
}
```

Đây là bug hiệu năng **không compiler nào cảnh báo, không sanitizer nào bắt**. Code vẫn *đúng*, chỉ *chậm*. Nó sống sót hàng năm trong production.

*(Lưu ý: nếu `T` là template parameter thì `T&&` là **forwarding reference**, và ở đó phải dùng `std::forward<T>(param)`, không phải `std::move`. Chi tiết ở **W6**.)*

---

## 4. Vì sao `std::vector` copy khi move constructor thiếu `noexcept`?

Xuất phát điểm: **`vector` cam kết strong exception guarantee khi realloc** — *nếu có exception, container phải nguyên vẹn như chưa từng đụng vào.*

Giả sử đang chuyển 100 phần tử sang buffer mới, và **phần tử thứ 50 ném exception**:

### Nếu nó MOVE → hỏng không cứu được
49 phần tử đầu đã bị **rút ruột**. Buffer cũ giờ toàn xác `<moved-from>`. Buffer mới thì dở dang.
**Không còn đường lùi** — muốn khôi phục buffer cũ thì phải move ngược lại, mà thao tác move ngược đó **cũng có thể ném tiếp**.
→ Dữ liệu bốc hơi. Strong guarantee **bị phá vỡ**.

### Nếu nó COPY → vẫn cứu được
Copy **không đụng gì tới nguồn**. Buffer cũ **nguyên vẹn 100%**.
`vector` chỉ cần: huỷ buffer mới, ném exception lên, xong. **Không ai biết chuyện gì vừa xảy ra.**
→ Strong guarantee **được giữ**.

### Nên logic của vector là

> *"Move constructor của mày có **hứa** không bao giờ ném không?
> Có (`noexcept`) → tao **move**, nhanh.
> Không → tao đành **copy**, chậm, nhưng an toàn."*

### Cơ chế trong thư viện chuẩn

**`std::move_if_noexcept`** — sẽ **tự tay viết lại ở W5** khi build `Vector`.

### Hệ quả thực tế

`std::vector<MyType>` một triệu phần tử. Ai đó quên một chữ `noexcept`.
→ Mỗi lần realloc: **một triệu lần copy thay vì move**.
Code vẫn đúng. Test vẫn xanh. Sanitizer vẫn im. Chỉ chậm gấp hàng chục lần, và **không công cụ nào chỉ ra**.

> **`noexcept` trên move constructor không phải trang trí. Nó là hợp đồng mà thư viện chuẩn đọc và ra quyết định dựa trên đó.**

Đó là lý do `.clang-tidy` (Week 0) bật `performance-noexcept-move-constructor` ở mức **WarningsAsErrors**.

---

## 5. Khi nào **KHÔNG** nên dùng `std::move`

1. **`return std::move(local);`** — **giết NRVO**, biến 0 thao tác thành 1 move. *Tự đo được ở bằng chứng (B).* `-Wpessimizing-move` bắt được cái này.

2. **Trên object `const`.** `std::move(const T)` cho ra `const T&&`, mà cái đó **bind vào `const T&`** → gọi **copy constructor**. Im lặng, không cảnh báo. Bạn tưởng đang move, thực ra đang copy.

3. **Trên object bạn còn dùng sau đó.** Sau move, nguồn ở trạng thái *valid but unspecified*. Đọc nó là **bug logic** (không phải UB, nhưng tệ ngang).

4. **Trên forwarding reference (`T&&` trong template).** Phải dùng **`std::forward<T>`**. `std::move` sẽ cướp cả những lvalue mà caller còn cần → hỏng dữ liệu của người ta.

5. **Trên type trivially-copyable** (`int`, POD, con trỏ thô). Move của chúng **chính là** copy. Không lợi gì, chỉ làm code ồn.

---

## 6. Trade-offs

| Tình huống | Dùng gì | Vì sao |
|---|---|---|
| Trả về local | `return local;` | NRVO — **0 thao tác**. Thêm `std::move` là tự bắn vào chân. |
| Chuyển tiếp `T&&` **không** phải template | `std::move(param)` | `param` có tên → lvalue → không move thì copy |
| Chuyển tiếp `T&&` **trong** template | `std::forward<T>(param)` | Giữ nguyên value category của caller |
| Move ctor tự viết | **luôn `noexcept`** | `vector` đọc nó để quyết định move hay copy |
| Type nhỏ, trivially copyable | không cần gì | move ≡ copy |

---

## 7. Câu hỏi còn nợ

### `std::move` thực chất làm gì?

**Nó không move gì cả.** Nó là một **cast**, thuần tuý, hết:

```cpp
template <typename T>
constexpr std::remove_reference_t<T>&& move(T&& t) noexcept {
    return static_cast<std::remove_reference_t<T>&&>(t);
}
```

Nó chỉ **nói dối compiler rằng "thằng này sắp chết rồi"**, để overload resolution chọn move constructor thay vì copy constructor. Việc *chuyển dữ liệu thật* là do **move constructor** làm, không phải `std::move`.

Tên gọi `std::move` là một trong những **cái tên tệ nhất lịch sử C++**. Đúng ra nó phải tên là `rvalue_cast`.

### `return local;` khi `local` là **tham số** — còn NRVO không?

**Không.** NRVO đòi hỏi compiler dựng thẳng object vào **return slot** của caller. Nhưng **tham số đã nằm sẵn ở chỗ khác rồi** (do caller đặt vào, theo ABI) — không thể xây nó ở nơi nó đã tồn tại.

Nhưng bạn **không bị copy**: từ C++11, `return param;` được **implicit move** (compiler tự coi `param` là rvalue khi return). Nên:

- Trả local → **0 thao tác** (NRVO)
- Trả tham số → **1 move** (implicit move, không elide được)
- Và **vẫn tuyệt đối đừng viết `return std::move(param);`** — thừa, mà lại chặn tối ưu khác.

### C++17 làm gì với prvalue?

Trước C++17: `T x = T();` tạo ra một **temporary**, rồi copy/move nó vào `x`. Compiler **được phép** bỏ qua bước đó (copy elision) — nhưng chỉ là **được phép**, và copy/move constructor **vẫn phải tồn tại** dù không được gọi.

Từ C++17: prvalue **không còn là object** nữa. Nó chỉ là **"công thức để khởi tạo"** (initializer for a result object), và việc **vật chất hoá** (materialization) bị **hoãn** đến khi thật sự cần.

→ Trong `T x = T();` **không hề có temporary nào để mà elide**. Copy elision cho prvalue trở thành **BẮT BUỘC**, không còn là tuỳ chọn. Type thậm chí **không cần** copy/move constructor — có thể là **immovable** vẫn chạy.

**Cực kỳ quan trọng:** điều này **chỉ đúng cho prvalue**. **NRVO** (trả về *tên* của local) đến C++23 **vẫn chỉ là tuỳ chọn** — compiler *được phép* nhưng *không bắt buộc*. GCC/Clang có làm ở `-O0`, nhưng đó là lòng tốt, không phải luật.

Đây là cửa vào **W43 (Copy Elision / RVO / NRVO)**.
