# W15 — AVL Tree (cây tự cân bằng) → map / set

> Mục **6** để trống — tự viết.

---

## 0. Bằng chứng (tự đo)

```
chèn 1,000 key tăng dần → chiều cao AVL = 10   (log2 = 10.0, BST suy biến = 1000)
chèn 100,000            → chiều cao AVL = 17   (log2 = 16.6, BST suy biến = 100000)
chèn 1,000,000          → chiều cao AVL = 20   (log2 = 19.9, BST suy biến = 1000000)
```
Chèn tăng dần = worst case của BST. BST thường → list dài 1 triệu → tìm O(n). AVL giữ
chiều cao ~20 → tìm 20 bước. Nhanh gấp ~50.000 lần, chỉ nhờ xoay.

Test #5: cùng thứ tự sắp xếp + cùng value như `std::map` trên 5000 key ngẫu nhiên.

---

## 1. BST — ý tưởng gốc

```
      5          trái < node < phải
     / \.
    3   8        tìm = mỗi bước loại NỬA cây → O(log n)
   / \.  \.
  1   4   9      in-order (trái→node→phải) → dãy SẮP XẾP: 1,3,4,5,8,9
```
Điểm mạnh so với hash: **luôn có thứ tự**. Hash rải key khắp nơi, không có thứ tự.

## 2. Vấn đề: suy biến thành list

Chèn 1,2,3,4,5 tăng dần vào BST thường → cây nghiêng hẳn một bên → **linked list** →
tìm O(n). Mà dữ liệu sắp xếp lại rất phổ biến → BST thường thường vô dụng thực tế.

## 3. AVL: giữ cân bằng

**Bất biến:** mọi node, chiều cao trái − phải ∈ {-1, 0, 1} (balance factor).
Vi phạm (bf > 1 hoặc < -1) → **xoay** (O(1)) lập lại cân bằng.

**Balance factor** = `height(left) - height(right)`. Là "cảm biến lệch".
**updateHeight** = `1 + max(height con)`. Gọi sau MỌI thay đổi, nếu không bf đọc số cũ.

**Rotation (rotateRight):** con trái đôn lên làm gốc, node cũ tụt xuống làm con phải.
```
    y            x
   / \.         / \.
  x   C  -->   A   y      giữ nguyên thứ tự: A < x < B < y < C
 / \.             / \.
A   B            B   C
```
Node là `unique_ptr` → xoay = `std::move` chuyển quyền sở hữu (W3+W4). Không copy, không
leak — RAII tự dọn cả cây khi root_ chết (Rule of Zero, không viết destructor nào).

**4 ca:** LL (1 xoay phải), RR (1 xoay trái), LR (xoay trái nhánh trái → rồi phải),
RL (đối xứng). LR/RL là ca "gãy khúc" — biến thành LL/RR trước.

**insert đệ quy:** đi xuống (rẽ trái/phải) → tạo node ở nullptr → **bung lên**, mỗi tầng
updateHeight + rebalance. Một chèn = một đường xuống + một đường lên.

## 4. map (cây) vs unordered_map (hash)

| | map (cây) | unordered_map (hash) |
|---|---|---|
| tìm/chèn/xoá | O(log n) | O(1) trung bình |
| **thứ tự** | ✅ luôn sắp xếp | ❌ hỗn loạn |
| **range query** `[10,50]` | ✅ lower/upper_bound | ❌ quét hết |
| worst case | O(log n) **đảm bảo** | O(n) nếu hash tệ |
| cache | kém (node rải rác) | tốt hơn (open addr) |

Chọn `map` khi cần: duyệt theo thứ tự, tìm khoảng, hoặc worst-case đảm bảo (real-time).

## 5. std::map dùng Red-Black, không AVL — vì sao?

Cùng ý tưởng (tự cân bằng), luật khác:
- **AVL** cân bằng *chặt hơn* (chiều cao thấp hơn) → **tìm nhanh hơn**.
- **RB** cân bằng *lỏng hơn* → **ít xoay hơn khi chèn/xoá** → **sửa đổi nhanh hơn**.

`std::map` chọn RB vì cân bằng giữa tìm và sửa. AVL tốt hơn khi tìm nhiều, sửa ít.

## 6. Vì sao thứ tự đáng giá cái O(log n) — **TỰ WRITE**

Gợi ý: hash cho O(1) nhưng mất gì? Ba việc `map` làm được mà `unordered_map` không?
Khi nào chấp nhận chậm hơn để có thứ tự? (gợi ý: range query, "phần tử lớn nhất/nhỏ
nhất", "key gần nhất", worst-case đảm bảo)

**Trả lời:**




---

## 7. Câu hỏi phỏng vấn tiếp theo

- AVL vs Red-Black: cái nào tìm nhanh hơn, cái nào chèn/xoá nhanh hơn? Vì sao?
- `std::map::lower_bound(k)` làm gì, và vì sao chỉ cây làm được O(log n)?
- Xoá trong AVL phức tạp hơn chèn thế nào? (có thể cần nhiều xoay trên đường về gốc)
- Vì sao cây (node rải rác) chậm hơn hash ở lookup thuần? (cache — W13/W14 lặp lại)
- B-tree khác BST gì? Vì sao database/filesystem dùng B-tree chứ không AVL?
  (gợi ý: đĩa đọc theo block → muốn nhiều key mỗi node → ít lần đọc đĩa)
