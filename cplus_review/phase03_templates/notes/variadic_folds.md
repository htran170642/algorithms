# W22 — Variadic Templates · Pack Expansion · Fold Expressions

> Mục **8** để trống — tự viết.

---

## 0. Bằng chứng

```
23/23 test xanh — debug/asan/ubsan × C++20 và C++23 (ALL GREEN)
```
Artifact: `include/w22/print.hpp` — `printAll`, `print`, `format`, `Tuple`.
Ba lỗi build đầu tiên đều nói về **cùng một sự thật** (pack rỗng nở ra thành *không gì cả*).
Xem mục 5.

---

## 1. Pack là gì — **hai cái túi**

```cpp
template <typename... Ts>
void f(Ts... ts) { }

f(1, 2.5, "hi");
```
```
túi Ts  =  { int , double , const char* }      ← túi KIỂU
túi ts  =  { 1   , 2.5    , "hi"        }      ← túi GIÁ TRỊ
```

Hai túi **luôn cùng số phần tử**; phần tử thứ i của túi này đi với phần tử thứ i của túi kia.

**`ts` không phải một biến — nó là ba biến.** Nên:
```cpp
std::cout << ts;     // ❌ LỖI. Túi vẫn đóng.
```
Pack chỉ tồn tại lúc biên dịch. Nó **luôn phải nở ra** trước khi thành code thật.

---

## 2. ⭐ Thủ tục tìm MẪU (phần khó nhất tuần này)

`...` là **máy photocopy**: *"chép lại **toàn bộ** mẫu, lần thứ i thay mỗi túi bằng phần tử
thứ i, các bản ngăn nhau bằng dấu phẩy."*

### Trước hết: fold hay photocopy?

Nhìn ký tự **sát bên trái** dấu `...`:

| Sát trái `...` | Là gì | Xử lý |
|---|---|---|
| toán tử (`+`, `<<`, `&&`, `,`) | **FOLD** | nối các phần tử bằng toán tử đó → mục 3 |
| bất kỳ thứ gì khác | **photocopy** | chạy thủ tục dưới đây |

```cpp
(ts + ...)      // sát trái là "+"  → FOLD
(ts * 2)...     // sát trái là ")"  → photocopy
```

### Thủ tục: đặt ngón tay lên `...`, đi LÙI sang trái

Dừng khi gặp **một trong ba biển báo**:
```
①  dấu phẩy   ,
②  dấu mở ngoặc CHƯA ĐÓNG    (   {   [
③  hết biểu thức
```
**Luật phụ:** cặp ngoặc **đã khớp** thì nhảy qua nguyên cụm, không chui vào trong —
nên dấu phẩy *bên trong* cụm đó không tính.

| Biểu thức | Đi lùi gặp | MẪU |
|---|---|---|
| `g( h(ts)... )` | `)` khớp → nhảy `h(ts)`; rồi `(` của `g` → ② | `h(ts)` |
| `{ (ts * 2)... }` | `)` khớp → nhảy `(ts*2)`; rồi `{` → ② | `(ts * 2)` |
| `{ ts... , 99 }` | `ts`; rồi `{` → ② | `ts` |
| `f( a, ts..., b )` | `ts`; rồi `,` → ① | `ts` |
| `foo( bar(ts, 7)... )` | `)` khớp → nhảy cả cụm (**dấu phẩy bên trong vô hình**) | `bar(ts, 7)` |
| `target( std::forward<Args>(args)... )` | `)` khớp → nhảy `(args)`; qua `std::forward<Args>`; rồi `(` của target → ② | `std::forward<Args>(args)` |

### Hai bẫy chết người

**Bẫy 1 — cắt mẫu quá ngắn.**
```cpp
{ (ts * 2)... }    →  { 2, 4, 6 }        ✅ mẫu = "(ts * 2)"
                   ✗  { 1, 2, 3, 2 }     ❌ nếu tưởng mẫu chỉ là "ts"
```
> **Mẫu không bao giờ bị cắt đôi.** `...` không dính vào tên biến — nó dính vào **cả biểu
> thức** đứng trước.

**Bẫy 2 — vị trí `...` đổi số lần gọi.**
```cpp
g( h(ts)... )   →  g( h(1), h(2), h(3) )     h chạy 3 lần   (... NGOÀI h)
g( h(ts...) )   →  g( h(1, 2, 3) )           h chạy 1 lần   (... TRONG h)
```
Dịch một ký tự → nghĩa đổi hoàn toàn.

### Mẫu chứa **hai** túi → nở song song, khoá bước

```cpp
target( std::forward<Args>(args)... )
        └────────── mẫu ────────┘

bản 1:  std::forward<int>(a1)
bản 2:  std::forward<double>(a2)      ← A2 đi với a2, KHÔNG BAO GIỜ với a1
bản 3:  std::forward<string>(a3)
```
Không phải tích Descartes. Vì sao **không** viết `std::forward<Args...>(args...)`? Vì ở đó
`...` nằm **trong** — giống `h(ts...)` — nghĩa là gọi `forward` **một lần** với cả 3 kiểu và
cả 3 giá trị. `forward` nhận 1 + 1 → lỗi.

---

## 3. Fold expression — nối bằng toán tử thay vì dấu phẩy

```cpp
ts...           →   1, 2, 3        (dấu phẩy — photocopy)
(ts + ...)      →   1 + 2 + 3      (dấu cộng — fold)
```
**Ngoặc tròn ngoài cùng là bắt buộc, thuộc về cú pháp fold** (xem lỗi 5.3).

### Bốn dạng

```cpp
(  pack op ...          )    unary  right   a1 op (a2 op a3)
(  ...  op pack         )    unary  left    (a1 op a2) op a3
(  pack op ...  op init )    binary right   a1 op (a2 op (a3 op init))
(  init op ...  op pack )    binary left    ((init op a1) op a2) op a3
```

> **Mặc định chọn LEFT fold** — khớp thứ tự đánh giá trái→phải. Với `-` thì right fold cho
> `(ts - ...)` trên `{1,2,3}` ra `1-(2-3) = 2`, không phải `-4`.

### ⭐ Pack rỗng — chuẩn chỉ cứu **ba** toán tử

`(ts + ...)` với 0 phần tử phải cho ra **một giá trị**. Compiler không biết viết gì:
```
error: fold of empty expansion over operator+
```
Chuẩn định nghĩa sẵn giá trị rỗng cho **đúng ba**:

| Toán tử | Pack rỗng cho | |
|---|---|---|
| `&&` | `true` | phần tử trung hoà của AND |
| `\|\|` | `false` | phần tử trung hoà của OR |
| `,` | `void()` | không có giá trị nào để trả |

Mọi toán tử khác → **lỗi biên dịch**. Cách sửa: **tự cấp giá trị khởi tạo** bằng binary fold.
```cpp
(ts + ...)          // pack rỗng => LỖI
(ts + ... + 0)      // pack rỗng => 0     ✅
```

Trong artifact, đúng thủ thuật đó cứu `printAllTo`:
```cpp
(os << ... << ts)   // binary LEFT fold; giá trị khởi tạo chính là `os`
                    // pack rỗng => cả biểu thức thu về `os`, không ghi gì
```

**Callback W21:** một hàm chứa `(ts + ...)` **biên dịch bình thường** và chạy được với
`(1,2,3)`. Lỗi chỉ nổ khi instantiate với pack rỗng — **lazy instantiation**, mục 4 note W21.

---

## 4. `sizeof...` ĐẾM, `sizeof` ĐO

```cpp
sizeof(x)        // x chiếm bao nhiêu BYTE
sizeof...(ts)    // túi ts có bao nhiêu PHẦN TỬ
```
```
f(1, 2.5, "hi")   →   sizeof...(ts) = 3
                      4 + 8 + 8     = 20    ← số mà cái tên dụ bạn nghĩ tới
```
Dùng được với **cả hai** túi: `sizeof...(ts)` (giá trị) lẫn `sizeof...(Ts)` (kiểu).
Trùng chữ cái, không liên quan gì nhau.

---

## 5. ⭐ Ba lỗi build — cả ba nói cùng một chuyện

### 5.1 Fold rỗng thu về đúng giá trị khởi tạo
```
error: statement has no effect [-Werror=unused-value]
   (os << ... << ts);
```
Chỉ nổ ở instantiation `Ts = {}`. Với pack rỗng, fold **thật sự** thu về biểu thức trần `os`
— một câu lệnh không làm gì. **GCC vừa tự chứng minh điều mục 3 khẳng định.**
Sửa: `static_cast<void>((os << ... << ts));`

### 5.2 Pack rỗng nở ra thành *không một dòng code nào*
```
error: variable ‘emit’ set but not used [-Werror=unused-but-set-variable]
```
`format("plain")` có pack rỗng → `(emit(ts), ...)` nở ra **rỗng** → lambda không bao giờ được
gọi. Sửa: `[[maybe_unused]]` — *"tôi biết, đó là chủ đích"* (đúng như W21 §5.3).

### 5.3 Ngoặc của fold là CÚ PHÁP, không phải để nhóm
```
error: expected ‘)’ before ‘<<’ token
   static_cast<void>(os << ... << ts);
```
`static_cast<void>(...)` **mượn mất** cặp ngoặc mà fold cần → parser không còn nhận ra đó là
fold. Sửa: dùng **hai** cặp — `static_cast<void>((os << ... << ts))`.

> Cùng họ với W21 §5.1 (macro và dấu phẩy): **ngoặc trong C++ template không phải lúc nào
> cũng chỉ để nhóm — đôi khi nó là một phần của cú pháp.**

---

## 6. Bài toán dấu phân cách — và vì sao comma fold quan trọng

Dấu cách nằm **giữa** các phần tử → phần tử **đầu tiên** phải hành xử khác. Không có nhánh
runtime nào làm được. Cách giải: **tách phần tử đầu ra khỏi pack ngay ở chữ ký**.

```cpp
template <typename First, typename... Rest>
void printTo(std::ostream& os, const First& first, const Rest&... rest) {
    os << first;                    // không dấu cách trước
    ((os << ' ' << rest), ...);     // n-1 dấu cách, đúng theo cấu trúc
    os << '\n';
}
inline void printTo(std::ostream& os) { os << '\n'; }   // pack rỗng
```
Overload không-template bắt trường hợp 0 tham số — template cần ít nhất `First` nên **không
khả dụng**, không phải "thua" (W21 §3.2: nó còn chẳng vào được đấu trường).

### ⭐ Comma fold là expansion DUY NHẤT đảm bảo thứ tự

```cpp
(emit(ts), ...);          // ✅ trái → phải, ĐƯỢC BẢO ĐẢM
foo(emit(ts)...);         // ❌ thứ tự đánh giá tham số hàm là KHÔNG XÁC ĐỊNH
```
`emit` sửa biến `pos` → thứ tự **bắt buộc** phải đúng. Nếu nở vào danh sách tham số, code
chạy đúng trên GCC và sai trên compiler khác — loại bug tệ nhất.

> **Có side effect trong mẫu → dùng comma fold, không nở vào lời gọi hàm.**

---

## 7. `Tuple` — nơi fold bất lực

```cpp
template <typename... Ts> struct Tuple { };              // đuôi rỗng
template <typename Head, typename... Tail>
struct Tuple<Head, Tail...> {                            // partial specialization
    Head           head;
    Tuple<Tail...> tail;
};
```
**Không thể lưu một pack.** `Ts... ts;` không phải khai báo thành viên hợp lệ. Tuple phải
biến pack thành **chuỗi kiểu lồng nhau** head + tail, và duyệt bằng **đệ quy** (`get<N>` +
`if constexpr`).

> Fold **gộp** nhiều giá trị thành **một**. Tuple phải **giữ riêng** từng phần tử.
> Đó là ranh giới: cần một kết quả → fold; cần giữ cả n → đệ quy trên kiểu.

Và đây là chỗ **specialization đúng nghĩa** — class template, an toàn tuyệt đối (W21 §3.5).

---

## 8. Vì sao `format()` **buộc** phải dùng comma fold — **TỰ VIẾT**

Gợi ý: `emit` bắt `pos` theo tham chiếu và **sửa** nó mỗi lần gọi. Nếu viết
`helper(emit(ts)...)` thay vì `(emit(ts), ...)` thì chuyện gì xảy ra trên một compiler đánh
giá tham số từ **phải sang trái**? Kết quả `format("{} + {} = {}", 1, 2, 3)` sẽ là gì?
Vì sao chuẩn **không** quy định thứ tự đánh giá tham số hàm, nhưng **có** quy định cho toán
tử phẩy? Bug loại này vì sao nguy hiểm hơn một lỗi biên dịch?

**Trả lời:**




---

## 9. Câu hỏi phỏng vấn tiếp theo

- `Ts...` vs `Ts&&...` — khi nào dùng cái nào? (`Ts&&...` = universal reference, cần
  `std::forward` → W6; ở đây `const Ts&...` vì chỉ đọc để in)
- Nở pack vào **template argument list** thì sao? `std::tuple<Ts...>` khác `Base<Ts>...` gì?
- Vì sao `std::format` (C++20) kiểm tra được chuỗi định dạng lúc **biên dịch** còn `format()`
  ở đây thì không? (`consteval` + `std::format_string`, W10)
- Làm sao `static_assert` số `{}` khớp `sizeof...(Ts)`? (cần `fmt` là `constexpr`)
- Nở pack trong **lambda init-capture**: `[...xs = ts]` (C++20) dùng khi nào?
- `std::index_sequence` / `std::make_index_sequence` để làm gì? Vì sao cần nó để duyệt tuple?
- Vì sao đệ quy trên pack **chậm biên dịch** hơn fold? (mỗi bậc là một instantiation mới)
- C++17 mới có fold — trước đó người ta làm thế nào? (`int dummy[] = {(f(ts), 0)...}` —
  trick kinh điển, và vì sao nó xấu)
