# W16 — Binary Heap → priority_queue

> Mục **5** để trống — tự viết.

---

## 0. Bằng chứng

```
top() = 9  (max luôn ở gốc, O(1))

Cây nhị phân trong MẢNG PHẲNG (không con trỏ):
      [9]              mảng: [9, 6, 5, 1, 4, 1, 2, 3]
     /   \                    0  1  2  3  4  5  6  7
   [6]   [5]
   / \   / \           con trái = 2i+1, con phải = 2i+2, cha = (i-1)/2
 [1][4][1][2]
   \
   [3]

pop lần lượt: 9 6 5 4 3 2 1 1   ← giảm dần
```
4/4 test xanh, gồm differential vs `std::priority_queue` và property-based.

---

## 1. Cây nhị phân nhét vào mảng — không con trỏ

**Max-heap:** cây nhị phân với một quy tắc: **cha ≥ con** (mọi node). → max luôn ở `arr[0]`.

Mẹo: quan hệ cha-con tính bằng **số học chỉ số**, không cần con trỏ:
```
node i:  con trái = 2i+1   con phải = 2i+2   cha = (i-1)/2
```
Cả cây gói trong một `std::vector` → **liền kề, cache-friendly** (W13 lần 3). Đây là lý do
heap nhanh và `priority_queue` chọn nó.

## 2. Ba thao tác

**`top()` — O(1):** max luôn ở gốc `arr[0]`. Đọc ngay.

**`push(x)` — O(log n):** thêm x vào **cuối mảng** → **sift-up**: so với cha, lớn hơn thì
đổi chỗ, lặp lên. Tối đa log n tầng.

**`pop()` — O(log n):** đưa **phần tử cuối** lên gốc, `pop_back`, rồi **sift-down**: so với
con LỚN hơn, nhỏ hơn thì đổi, lặp xuống.
> Không xoá gốc trực tiếp (để lỗ hổng) — đưa phần tử cuối lên giữ mảng liền kề.

## 3. Compare: less → max-heap (nghe ngược)

`Compare = std::less` → **max-heap**. Vì `comp_(parent, child)` = "parent < child" → cần
đổi để đẩy lớn lên trên. Truyền `std::greater` → min-heap. `std::priority_queue` cùng
convention.

## 4. Heap vs AVL cho priority queue

| | Heap | AVL |
|---|---|---|
| top (max) | **O(1)** | O(log n) |
| push/pop | O(log n) | O(log n) |
| bộ nhớ | mảng phẳng, 0 con trỏ | node + 2 con trỏ |
| cache | ✅ liền kề | ❌ rải rác |
| sắp xếp toàn bộ | ❌ chỉ biết max | ✅ |

Heap hy sinh thứ tự toàn bộ → đổi lấy top O(1), bộ nhớ gọn, cache tốt. Dùng khi chỉ cần
"lấy phần tử ưu tiên nhất liên tục": Dijkstra, top-K, scheduler.

## 5. container adapter: stack / queue / priority_queue

Không phải container thật — **adapter** bọc container khác, lộ giao diện hạn chế:
- `stack<T>` → bọc `deque`, LIFO
- `queue<T>` → bọc `deque`, FIFO
- `priority_queue<T>` → bọc `vector` + heap

Adapter pattern (→ Phase 9). `std::priority_queue` chính là heap này bọc quanh vector.

## 6. Vì sao heap thắng cây khi chỉ cần max — **TỰ WRITE**

Gợi ý: nếu chỉ cần lấy max liên tục (không cần tìm phần tử bất kỳ, không cần range), heap
cho gì mà AVL không? Nối với W13/W14 — điểm chung "mảng phẳng thắng node rải rác". Khi nào
KHÔNG dùng heap? (cần tìm/xoá phần tử bất kỳ giữa chừng)

**Trả lời:**




---

## 7. Câu hỏi phỏng vấn tiếp theo

- Heapsort: push hết rồi pop hết → O(n log n). Vì sao nó **không stable**? Vì sao ít
  dùng dù cùng độ phức tạp quicksort?
- `std::make_heap` xây heap từ mảng trong **O(n)**, không phải O(n log n). Vì sao?
  (sift-down từ dưới lên, đa số node ở tầng thấp)
- Xoá một phần tử **bất kỳ** (không phải max) khỏi heap — làm sao? Độ phức tạp?
- `std::priority_queue` có cho `top()` trả về non-const reference không? Vì sao không?
  (sửa top phá heap invariant)
- Fibonacci heap / pairing heap giảm decrease-key xuống O(1) amortized — dùng ở đâu?
  (Dijkstra với đồ thị dày)
