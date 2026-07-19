# W12 — vector / array / deque + Iterator Invalidation

> Mục **5** để trống — tự viết.

---

## 0. Bằng chứng (tự đo, xanh cả ASAN)

```
#1 reserve(100) -> push -> v.data() KHÔNG đổi   → con trỏ SỐNG    (không realloc)
#2 shrink_to_fit -> push -> v.data() ĐỔI        → buffer cũ CHẾT  (realloc, W9 lặp lại)
#4 deque push_front x100 -> p_to_20 vẫn = 20    → khối cũ không dời
#6 erase giữa vòng lặp -> it = v.erase(it)      → dùng iterator trả về (đúng)
```

---

## 1. Ba cấu trúc bộ nhớ

```
array<T,N>:  [a][b][c][d]          cố định lúc BIÊN DỊCH, trên stack, không heap
vector<T>:   [a][b][c][ ][ ]       một khối liền kề trên heap; đầy → realloc
deque<T>:    [→][→][→]             bảng con trỏ tới nhiều khối rời
              ↓   ↓   ↓
            [abc][def][ghi]
```

- **`array`** — nhanh nhất, cứng nhắc. `sizeof == N*sizeof(T)`, không bookkeeping.
- **`vector`** — liền kề (cache-friendly), random access O(1). Nhưng realloc dời cả buffer.
- **`deque`** — `push_front` VÀ `push_back` đều O(1). Random access `d[i]` chậm hơn
  vector chút (qua bảng khối). `queue`/`stack` mặc định dùng deque.

## 2. Iterator invalidation — SUY RA, không học thuộc

> **Nguyên tắc vàng:** cái gì làm **DỜI phần tử trong bộ nhớ** thì làm hỏng
> iterator/con trỏ trỏ tới nó. Hỏi "thao tác này có dời phần tử không?" là ra.

| Thao tác | vector | deque | list |
|---|---|---|---|
| push_back **có** realloc | ❌ hỏng hết | — | — |
| push_back **không** realloc | iterator hỏng*, con trỏ OK | — | — |
| push_front | (không có) | iterator hỏng, con trỏ OK | ✅ tất cả OK |
| insert/erase giữa | ❌ từ chỗ đó | ❌ hỏng hết | ✅ chỉ phần tử bị xoá |

*Chuẩn coi mọi push_back là invalidate iterator; nhưng con trỏ thô vào buffer vẫn
sống nếu KHÔNG realloc.

**Suy luận từ bộ nhớ:**
- vector realloc → **dời cả buffer** → mọi thứ vào buffer cũ chết (đây là W9)
- deque push_front → chỉ thêm khối mới, **khối cũ không dời** → con trỏ phần tử cũ sống
- list → mỗi phần tử là node riêng, thêm/xoá không đụng node khác → chỉ node bị xoá chết

## 3. Bẫy erase-trong-vòng-lặp (test #6)

```cpp
// SAI: it bị invalidate bởi erase, rồi ++it dùng iterator chết → UB
for (auto it = v.begin(); it != v.end(); ++it)
    if (*it % 2 == 0) v.erase(it);

// ĐÚNG: erase trả về iterator hợp lệ kế tiếp
for (auto it = v.begin(); it != v.end(); ) {
    if (*it % 2 == 0) it = v.erase(it);   // gán lại
    else              ++it;
}
```
Bug thật, xuất hiện trong code production suốt. (C++20: có thể dùng `std::erase_if(v, pred)`
gọn hơn.)

## 4. Khi nào dùng cái nào

| Cần | Dùng |
|---|---|
| size cố định, biết lúc compile | `array` |
| mặc định, random access, push_back | `vector` |
| push_front **và** push_back O(1) | `deque` |
| chèn/xoá giữa nhiều, không cần random access | `list` (nhưng đo trước — W13) |

## 5. Vì sao "cái gì dời phần tử thì giết con trỏ" là nguyên tắc đủ — **TỰ VIẾT**

Gợi ý: dùng nguyên tắc này giải thích cả 3 dòng — vì sao vector realloc giết hết, vì
sao deque push_front không giết con trỏ phần tử cũ, vì sao list erase chỉ giết đúng
node bị xoá. Một nguyên tắc, ba kết luận.

**Trả lời:**




---

## 6. Câu hỏi phỏng vấn — có đáp án

### 6.1 `reserve` vs `resize`

Đo được:
```
reserve(5): size=0 capacity=5   ← chỉ XIN CHỖ, 0 phần tử
resize(5):  size=5 capacity=5   ← TẠO 5 phần tử (giá trị 0)
```

- **`reserve(n)`** — cấp phát sẵn chỗ cho `n` phần tử, nhưng **size vẫn nguyên**. Không
  construct gì. Mục đích: tránh realloc khi bạn biết trước sẽ push bao nhiêu.
  `v[0]` sau `reserve` là **UB** (chưa có phần tử nào).
- **`resize(n)`** — thực sự **tạo/xoá phần tử** để size == n. Phần tử mới được
  value-init (0 với int). `v[0]` hợp lệ ngay.

> Bẫy: `reserve` rồi `v[i] = x` là **UB** (ghi vào chỗ chưa có phần tử). Phải
> `push_back`/`emplace_back`, hoặc dùng `resize` nếu muốn index trực tiếp.

### 6.2 Vì sao `shrink_to_fit` chỉ là *request*

Chuẩn nói `shrink_to_fit` là **non-binding request** — implementation **được phép bỏ
qua**. Lý do: co buffer đòi hỏi cấp buffer mới nhỏ hơn + copy/move sang + free cũ —
một thao tác **có thể ném** và **tốn kém**. Có thư viện chọn không làm nếu thấy không
lợi. Muốn chắc chắn co: dùng thủ thuật `vector<T>(v).swap(v)` (tạo bản sao khít rồi
swap) — nhưng cũng không tuyệt đối.

### 6.3 `deque[i]` chậm hơn `vector[i]` vì sao

- `vector[i]` = `*(data_ + i)` → **một phép cộng địa chỉ**, một lần chạm bộ nhớ.
- `deque[i]` = tìm khối chứa `i` (chia/lấy dư để ra chỉ số khối), đọc **bảng con trỏ**
  lấy địa chỉ khối, rồi mới index trong khối → **hai lần chạm bộ nhớ** + vài phép tính.

Cả hai vẫn O(1), nhưng hằng số của deque lớn hơn, và **phá cache** (bảng khối + khối ở
hai vùng nhớ khác nhau). Đo cụ thể ở **W13**.

### 6.4 Vì sao `insert` giữa `vector` là O(n), `list` O(1)

- `vector` liền kề → chèn vào giữa phải **dời tất cả phần tử phía sau** sang phải 1 ô
  → O(n) phép move. (Áp dụng nguyên tắc vàng: dời phần tử → cũng invalidate iterator.)
- `list` là node rời → chèn = **đổi vài con trỏ** (`prev`/`next` của 2 node lân cận),
  không đụng phần tử khác → O(1)... **nhưng** phải *tìm* chỗ chèn trước đã, mà tìm trong
  list là O(n) (đi bộ). Nên "list chèn O(1)" chỉ đúng khi bạn **đã có iterator** tới
  chỗ đó. → cú twist của W13.

### 6.5 `vector<bool>` — vì sao `&v[0]` không ra `bool*`

`std::vector<bool>` **không lưu `bool`**. Nó nhồi **8 bool vào 1 byte** (mỗi bool = 1
bit) để tiết kiệm 8 lần bộ nhớ. Nhưng C++ **không lấy được địa chỉ của một bit** — nên
`v[i]` không trả về `bool&` thật, mà trả về một **proxy object** (`vector<bool>::reference`,
đo được `sizeof == 16`) giả vờ là `bool&`.

Hệ quả:
```cpp
bool* p = &v[0];        // KHÔNG biên dịch — &proxy không phải bool*
auto&& r = v[0];        // r là proxy, không phải bool&
```

> `vector<bool>` là **lời nói dối kinh điển của STL**: nó mang tên container nhưng
> không phải container đúng nghĩa (không thoả yêu cầu container chuẩn). Cần mảng bool
> thật → dùng `std::vector<char>`, `std::deque<bool>`, hoặc `std::bitset<N>`.
