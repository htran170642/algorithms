# W2 — Constructors & Initialization

> Mục **6** để trống — đó là phần bạn tự suy ra được trong buổi học, và cũng là
> mục **hữu dụng nhất trong đời thực**. Tự viết. 3–4 dòng là đủ.

---

## 0. Bằng chứng (đã tự chạy — `tests/init_test.cpp`, 7/7 PASS)

```cpp
std::vector<int> a(10, 5);   // size 10, toàn số 5      → (count, value)
std::vector<int> b{10, 5};   // size 2,  đúng {10, 5}   → initializer_list

std::vector<int> v1(3);      // size 3: {0, 0, 0}
std::vector<int> v2{3};      // size 1: {3}             ← THE CRUELEST PAIR
```

`explicit` chặn được chuyển đổi ngầm:
```
error: could not convert '42' from 'int' to 'Explicit'
```

`{}` chặn được narrowing (`()` thì không):
```
warning: narrowing conversion of 'p' from 'double' to 'int' [-Wnarrowing]
```
→ và vì project bật **`-Werror`** (Week 0), cái warning này **fail build**.

---

## 1. `initializer_list` ăn trọn

> Khi dùng **`{}`**, constructor nhận `std::initializer_list` được ưu tiên **áp đảo**.
> Compiler xét nó **trước tiên**; nếu nó khả dụng *bằng bất cứ giá nào*, các
> constructor khác **thậm chí không được xem xét**.

```cpp
struct Greedy {
    Greedy(int, int);                       // khớp hoàn hảo
    Greedy(std::initializer_list<int>);     // nhưng thằng này vẫn thắng
};

Greedy parens(1, 2);   // -> Greedy(int, int)
Greedy braces{1, 2};   // -> initializer_list
```

Đây là lời giải cho `vector<int> v2{3}` → `size == 1`: `vector` **có**
`initializer_list<int>` ctor, nên `{3}` nghĩa là *"một phần tử mang giá trị 3"*,
không phải *"ba phần tử"*.

---

## 2. `explicit` — constructor 1 tham số là một cái bẫy sập

> **Một constructor một tham số, không `explicit`, CHÍNH LÀ một toán tử chuyển đổi ngầm.**

Bạn tưởng mình đang định nghĩa *cách khởi tạo*. Thực ra bạn đang **cấp giấy phép cho
compiler biến `int` thành `Timeout` ở bất cứ đâu nó thấy tiện**.

```cpp
class Timeout { public: Timeout(int seconds); };   // không explicit
void wait(Timeout t);

wait(30);    // BIÊN DỊCH SẠCH. Compiler tự dựng Timeout(30). Không hỏi ai.
```

`30` là giây? mili-giây? Hay một biến `int` lạc vào? Không ai biết.
Code chạy, chỉ **sai ngữ nghĩa** — và **không sanitizer nào bắt được** loại lỗi này.

**Sửa:**
```cpp
explicit Timeout(int seconds);

wait(30);            // lỗi biên dịch  ← tốt
wait(Timeout{30});   // phải nói ra ý định
```

> **Quy tắc:** constructor **một tham số** → mặc định gắn `explicit`.
> Chỉ bỏ đi khi **thật sự muốn** chuyển đổi ngầm (ví dụ `std::string` từ `const char*`).

*(`.clang-tidy` của Week 0 có `google-explicit-constructor` để bắt chuyện này.)*

---

## 3. Most Vexing Parse

```cpp
Widget w1();    // KHÔNG phải Widget. Là KHAI BÁO HÀM:
                //   hàm tên w1, không tham số, trả về Widget.
Widget w2{};    // value-init  → Widget thật
Widget w3;      // default-init → Widget thật
```

Luật của C++: **cái gì *có thể* được hiểu là khai báo hàm, thì nó **là** khai báo hàm.**
Di sản từ C. `{}` miễn nhiễm với bẫy này.

---

## 4. Default-init vs Value-init

```cpp
struct Pod { int a; bool b; };

Pod value{};     // value-init  → a = 0, b = false
Pod garbage;     // default-init → a, b KHÔNG XÁC ĐỊNH. Đọc là UB.
```

**Quan trọng — bài học Phase 10:** **ASAN và UBSAN KHÔNG bắt được** việc đọc biến
chưa khởi tạo. Cần **MemorySanitizer** (chỉ Clang) hoặc **Valgrind**.

> Sanitizer không phải thuốc tiên. Biết **công cụ nào bắt được lỗi gì** là một phần
> của nghề.

---

## 5. Delegating constructor — viết invariant MỘT lần

```cpp
Connection(std::string host, int port) : host_(std::move(host)), port_(port) {
    if (port_ <= 0) throw std::invalid_argument("port must be positive");
}

// Uỷ quyền. Validation ở trên KHÔNG bị lặp lại → không thể lệch pha.
explicit Connection(std::string host) : Connection(std::move(host), 8080) {}
```

Không có delegating ctor, bạn phải copy-paste validation vào từng constructor — và
đến constructor thứ ba thì một cái sẽ quên, rồi invariant vỡ.

---

## 6. `{}` hay `()`? — **TỰ VIẾT**

Bạn đã tự suy ra được điều này. Viết lại bằng lời bạn.

Gợi ý cấu trúc:
- `{}` hơn `()` ở hai điểm nào? (nhớ narrowing + most vexing parse)
- `{}` phản chủ khi nào? Điều kiện chính xác là gì?
- Phát biểu một quy tắc duy nhất, dùng được hàng ngày.

**Trả lời:**




---

## 7. Câu hỏi phỏng vấn tiếp theo

- Vì sao `std::string s = "hi";` chạy được, nếu constructor 1 tham số nên `explicit`?
  (`std::string(const char*)` **cố tình** không explicit. Khi nào ngoại lệ là hợp lý?)
- `explicit` trên constructor **nhiều tham số** có ý nghĩa gì không? *(có — từ C++11, vì `{}`)*
- C++20 có `explicit(bool)` — *conditional explicit*. Nó giải quyết vấn đề gì?
- Vì sao `std::vector<std::string> v{3}` **không** biên dịch được, trong khi
  `std::vector<int> v{3}` thì được?
