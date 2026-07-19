# W11 — Iterators · `<algorithm>` · Ranges

> Mục **5** để trống — tự viết.

---

## 0. Bằng chứng

`std::sort` **từ chối** `std::list`:
```
error: no match for 'operator-' (std::_List_iterator<int>)
```
`std::sort` cần `it2 - it1` để chia đôi; list không có → không biên dịch.

`std::sort` **chấp nhận** iterator TỰ VIẾT (`IntRange::Iterator`) → test xanh. Vì nó
cung cấp đủ giao ước: 5 typedef + `+`, `-`, `<=>`, `[]`.

---

## 1. Iterator = con trỏ tổng quát hoá

Con trỏ làm 3 việc: `*p` đọc, `++p` đi, `p != end` so sánh. Iterator bắt chước đúng 3
thao tác đó cho **mọi** container.

> Ý tưởng lớn nhất của STL: **thuật toán tách rời cấu trúc dữ liệu**, nối với nhau chỉ
> bằng iterator. `std::sort` không biết `vector` là gì — nó chỉ biết begin/end/`*`/`++`.

## 2. Iterator categories — quyết định thuật toán nào chạy

| Category | Thêm được gì | Container |
|---|---|---|
| Input | đọc, `++`, một lần | stream |
| Forward | đọc/ghi, `++`, nhiều lần | `forward_list` |
| **Bidirectional** | thêm `--` (đi lui) | `list`, `map`, `set` |
| **Random access** | thêm `+n`, `-n`, khoảng cách, `[]` | `vector`, `deque`, mảng |

**Bidirectional vs Random access — bằng hình:**
```
vector:  [5][2][8][1]      liền kề → it+3 = 1 phép cộng (NHẢY)
list:    [5]─►[2]─►[8]─►[1]  node rải rác → phải ++ từng bước (ĐI BỘ)
```
`list` đi được **hai chiều** (`++`/`--`) nhưng **từng bước**, không nhảy. `std::advance(list_it, 3)`
vẫn chạy nhưng là O(n) (++ ba lần); với vector là O(1).

**Vì sao `std::sort` cần random access:** nó tính điểm giữa `first + (last-first)/2` để
phân hoạch → cần `+`/`-` → chỉ random access có → `list` phải tự có `.sort()` (merge
sort, chỉ cần `++`).

## 3. STL algorithms over manual loops (luật CLAUDE.md)

```cpp
// thủ công: dễ off-by-one, sai index
int c = 0; for (size_t i=0;i<v.size();++i) if (v[i]>10) ++c;
// STL: nói Ý ĐỊNH, không nói cơ chế
auto c = std::count_if(v.begin(), v.end(), [](int x){ return x>10; });
```
Ngắn hơn, khó sai hơn, đọc ra ý định. Dấu hiệu code C++ trưởng thành.

## 4. C++20 Ranges & Views

```cpp
std::ranges::sort(v);              // không cần begin/end

auto pipe = v | std::views::filter([](int x){ return x%2==0; })
              | std::views::transform([](int x){ return x*x; });
// {1..8} -> lọc chẵn -> bình phương -> {4,16,36,64}
```

**Views LAZY (lười):** không tạo container trung gian, không copy. `pipe` chưa tính gì
cho tới khi bạn duyệt. Ghép `filter | transform | take` thành pipeline, chạy khi đọc.
→ C++ đọc gần như Python nhưng vẫn nhanh.

## 5. Vì sao "giao ước iterator" khiến STL mạnh — **TỰ VIẾT**

Gợi ý: `std::sort` cần biết gì về `IntRange` để chạy? Nó có cần biết đó là mảng không?
5 typedef (`iterator_category`, `value_type`, ...) dùng để làm gì? Nếu bỏ
`iterator_category` đi thì sao?

**Trả lời:**




---

## 6. Câu hỏi phỏng vấn tiếp theo

- Vì sao thêm/xoá phần tử `vector` làm **invalidate** iterator, mà `list` thì không? (→ W12)
- `std::vector<bool>::iterator` — `*it` trả về cái gì? Vì sao nó **không** phải
  `bool&`? *(proxy iterator — vector<bool> là lời nói dối)*
- Sự khác biệt giữa `std::sort` (random access, O(n log n)) và `std::list::sort`
  (merge sort)? Cái nào ổn định (stable)?
- Views có sở hữu dữ liệu không? Điều gì xảy ra nếu container gốc chết trước view?
  *(dangling view — bẫy lifetime W9 tái xuất)*
- Khi nào một `transform | filter` pipeline **chậm hơn** một vòng for thủ công?
