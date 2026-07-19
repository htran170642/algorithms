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

## 6. Câu hỏi phỏng vấn tiếp theo

- `vector::reserve` vs `vector::resize` — khác gì? Cái nào tạo phần tử?
- Vì sao `vector::shrink_to_fit` là *request*, không phải lệnh? Compiler/lib có thể
  bỏ qua không?
- `deque[i]` chậm hơn `vector[i]` bao nhiêu, và vì sao? (→ đo ở W13)
- Vì sao `insert` vào giữa `vector` là O(n)? (dời phần tử) Còn `list` thì O(1)?
- `std::vector<bool>` — vì sao `&v[0]` không cho ra `bool*`? (proxy, không phải
  container thật — W11 đã nhắc)
