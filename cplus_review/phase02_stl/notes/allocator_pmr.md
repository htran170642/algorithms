# W20 — allocator + pmr · Đóng Phase 2

> Mục **5** để trống — tự viết.

---

## 0. Bằng chứng (tự đo)

```
BM_ListDefault      14,958 ns    ← mỗi node = 1 lần new
BM_ListMonotonic     6,808 ns    ← mỗi node = dời con trỏ → NHANH GẤP 2.2 LẦN
```
pmr::vector khác pool CÙNG kiểu: `static_assert(is_same_v<decltype(a), decltype(b)>)` xanh.
pmr::vector cấp phát từ buffer stack: `v.data()` nằm trong `buf` → không đụng heap.

---

## 1. Mọi container có allocator ẩn

```cpp
std::vector<int> v;                          // = std::vector<int, std::allocator<int>>
```
Allocator = *nguồn bộ nhớ*; container = *dùng bộ nhớ* (construct bằng placement new — W5).
Allocator chỉ **xin/trả bộ nhớ THÔ**: `allocate(n)` / `deallocate(p, n)`. Mặc định gọi
`::operator new`/`delete`.

## 2. Allocator cũ là một phần của KIỂU

```cpp
std::vector<int, AllocA>   // kiểu X
std::vector<int, AllocB>   // kiểu Y ← KHÁC → không gán được, bùng nổ template
```

## 3. pmr — allocator là CON TRỎ runtime (C++17)

```cpp
std::pmr::vector<int> a{&pool1};
std::pmr::vector<int> b{&pool2};   // pool khác, CÙNG kiểu → a = b biên dịch được
```
> pmr chuyển việc chọn allocator từ **compile-time (kiểu)** sang **runtime (con trỏ)**.
> Mọi `pmr::vector<int>` là một kiểu duy nhất. Đánh đổi: 1 virtual call mỗi cấp phát (W7).

**memory_resource có sẵn:**
- `monotonic_buffer_resource` — bump pointer trong buffer, KHÔNG free từng cái, xoá cả
  một lần. = **arena allocator** (tự viết ở W29).
- `unsynchronized_pool_resource` — pool các block cùng cỡ.

## 4. ⭐ BÀI HỌC: benchmark SAI cho kết quả NGƯỢC

Bản đầu so pmr vs default trên **vector** → pmr THUA (703 vs 562 ns). Hai lỗi:
1. `std::array<byte, 8000> buf{}` — `{}` **zero-init 8KB mỗi vòng**, chi phí này > cái pmr
   tiết kiệm.
2. `vector` chỉ ~log2(n) ≈ 10 lần cấp phát (geometric growth W5) → cấp phát KHÔNG phải
   bottleneck → pmr không có gì để tiết kiệm.

**Sửa:** dùng `list` (mỗi phần tử = 1 node = 1 `new`) + buffer cấp phát 1 lần ngoài vòng →
pmr thắng **2.2 lần**.

> pmr toả sáng khi có **NHIỀU cấp phát nhỏ** (node-based: list, map, set). Với vector thì
> gần như vô ích.
>
> **Một benchmark tồi nguy hiểm hơn không benchmark** — nó cho con số SAI với vẻ ngoài
> chính xác. Nếu tin con số đầu → kết luận "pmr vô dụng", hoàn toàn sai. Luôn hỏi: *phép
> đo này có đo đúng cái mình nghĩ không?* (→ W41 đến sớm)

## 5. Vì sao bump-pointer allocation nhanh hơn `new` — **TỰ WRITE**

Gợi ў: `new` phải làm gì (tìm chỗ, cập nhật heap, có thể lock)? monotonic_buffer làm gì
(chỉ...?)? Vì sao "không free từng cái" lại là ưu điểm chứ không phải nhược? Khi nào
monotonic KHÔNG dùng được? (cần free/tái dùng từng object giữa chừng)

**Trả lời:**




---

## 6. Câu hỏi phỏng vấn tiếp theo

- `monotonic_buffer_resource` khi hết buffer thì sao? (xin thêm từ upstream resource,
  mặc định là new_delete_resource)
- Vì sao allocator phải là stateless (hoặc `propagate_on_container_*`) trong allocator cũ?
- `std::pmr::polymorphic_allocator` — nó "xoá kiểu" allocator thế nào? (giữ con trỏ
  memory_resource*)
- Khi nào `unsynchronized_pool_resource` tốt hơn `monotonic`? (cần free/reuse block, đời
  sống object không đồng đều)
- Custom allocator cần `rebind` để làm gì? (list<T> cần cấp phát Node<T>, không phải T)
