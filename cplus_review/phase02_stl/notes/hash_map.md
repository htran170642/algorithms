# W14 — HashMap · Vì sao `std::unordered_map` chậm

> Mục **6** để trống — tự viết.

---

## 0. Bằng chứng (tự đo — `bench/bench_hashmap.cpp`, release-23)

```
BM_LookupOurs      77,526 ns     ← open addressing, tự viết
BM_LookupStd      186,493 ns     ← std::unordered_map → CHẬM GẤP 2.4 LẦN
```
100.000 lượt lookup. Bản viết trong một buổi thắng thư viện tối ưu 20 năm — vì được
dùng open addressing, còn `unordered_map` bị chuẩn cấm.

---

## 1. Ý tưởng: biến key thành chỉ số

```
"hello" → hash() → 8374625 → & mask → bucket[9]
```
Lookup = tính hash + truy cập mảng = **O(1)**. (`map` là cây → O(log n).)

**Mẹo `& mask` thay `% size`:** capacity luôn luỹ thừa 2 → `hash & (n-1) ≡ hash % n`
nhưng nhanh hơn hàng chục lần (AND vs chia). Đó là lý do `nextPow2()`.

## 2. Collision — hai trường phái

### Chaining — mỗi bucket là linked list
```
bucket[9] → [hello|42] → [world|7] → null
```
Đơn giản, chịu load factor cao. **Nhưng node rải rác trên heap → cache miss** (W13).

### Open addressing — tất cả trong MỘT mảng liền kề
```
[_][_][hello|42][world|7][_]   va chạm → tìm ô trống kế (linear probe)
```
**Cache-friendly.** Phải giữ load factor thấp (~0.7), xoá cần tombstone.

## 3. Ba trạng thái ô — và Tombstone

| Trạng thái | khi FIND | khi INSERT |
|---|---|---|
| Empty (chưa dùng) | DỪNG | ngồi vào được |
| Occupied | so key, sai thì đi tiếp | đi tiếp |
| **Tombstone** (đã xoá) | **ĐI TIẾP** | ngồi vào được |

**Vì sao cần Tombstone:** xoá ô GIỮA chuỗi probe mà đặt Empty → find dừng sớm ở đó →
phần tử phía sau **biến mất**. Đo được:
```
xoá key 8 (giữa), tìm key 16 (cuối chuỗi):
  đặt EMPTY     → find(16) = KHÔNG THẤY   ← BUG
  đặt TOMBSTONE → find(16) = 300          ← đúng
```
> Tombstone = "trống để ghi, nhưng không trống để tìm".

`rehash` **vứt bỏ mọi tombstone** (chỉ chèn lại Occupied) → dọn rác tự nhiên.

## 4. ⭐ Vì sao `std::unordered_map` CHẬM (câu phỏng vấn)

> Chuẩn C++ **BẮT BUỘC** `unordered_map` có **bucket interface**
> (`bucket_count`, `bucket(key)`) + **reference stability** (con trỏ phần tử không đổi
> khi rehash). Hai yêu cầu này **loại trừ open addressing** → buộc node-based.

Hệ quả: mỗi phần tử là một `new` riêng trên heap → mỗi lookup đi theo con trỏ →
**cache miss**. Cùng nguyên nhân W13.

> Bài học thiết kế API: một quyết định trong chuẩn (2011) **khoá hiệu năng vĩnh viễn** —
> không sửa được vì phá ABI. Đó là lý do Google viết `absl::flat_hash_map`, Facebook
> viết `F14`. `unordered_map` chậm không phải vì code kém, mà vì **API trói tay nó**.

## 5. Load factor & rehash
`loadFactor = size / capacity`. Vượt 0.7 → `rehash(cap*2)`: cấp mảng mới, **chèn lại
từng phần tử** (không copy thẳng được vì mask đổi → vị trí đổi). Đây cũng là lý do
`reserve()` quan trọng — tránh rehash lặp.

## 6. Vì sao open addressing thắng, và khi nào KHÔNG

### Điểm chung với W13: cache

W13 và W14 là **cùng một bài học**: **liền kề thắng node rải rác**, vì cache miss
(~100ns) đắt gấp ~100 lần cache hit (~1ns).

- W13: `vector` (liền kề) thắng `list` (node rải rác) khi duyệt/tìm.
- W14: open addressing (một mảng liền kề) thắng chaining (node rải rác) khi lookup.

Open addressing dò `ô kế tiếp` = đọc **cùng cache line** đã nạp → gần như miễn phí.
Chaining đi theo con trỏ tới node trên heap → **cache miss mỗi bước**. Đó là toàn bộ
2.4 lần chênh lệch.

### Khi nào chaining (node-based) TỐT HƠN

| Tình huống | Vì sao chaining thắng |
|---|---|
| **Cần reference stability** | node không dời khi rehash → con trỏ/iterator tới phần tử luôn hợp lệ. Open addressing rehash = dời cả mảng → mọi con trỏ chết. Đây chính là lý do `std::unordered_map` buộc chaining. |
| **Phần tử RẤT lớn** | open addressing lưu phần tử *trong* mảng → rehash phải copy/move cả phần tử lớn; chaining chỉ move con trỏ node. |
| **Load factor phải cao (>0.9)** | open addressing thoái hoá nặng khi gần đầy (probe dài); chaining vẫn chịu được vì mỗi bucket là list riêng. |
| **Xoá rất nhiều** | chaining xoá = tháo node, sạch sẽ; open addressing tích tombstone → phải rehash dọn. |

### Vì sao open addressing XOÁ phức tạp hơn

Vì phần tử có thể nằm **sau** vị trí lý tưởng của nó (do probe khi va chạm). Xoá một ô
giữa chuỗi mà đặt Empty sẽ **cắt đứt đường** tới phần tử phía sau → chúng biến mất
(đã đo ở mục 3). Nên phải dùng **tombstone** — thêm trạng thái thứ ba, thêm logic ở cả
find lẫn insert, và tombstone tích tụ làm chậm dần → phải rehash dọn.

Chaining không có vấn đề này: mỗi bucket là list độc lập, xoá một node **không ảnh
hưởng** node khác. Xoá O(1) sạch sẽ.

### Tóm tắt trade-off

> **Open addressing** = nhanh (cache), gọn bộ nhớ — nhưng cần load factor thấp, xoá
> phức tạp (tombstone), rehash giết con trỏ.
> **Chaining** = chậm hơn (cache miss) — nhưng reference stability, chịu load factor
> cao, xoá đơn giản.
>
> Đa số trường hợp thực tế: **open addressing thắng** (Google/Facebook đều chọn). Chuẩn
> C++ chọn chaining **không phải vì nó tốt hơn**, mà vì API `unordered_map` (2011) hứa
> reference stability — và giờ không rút lại được.

---

## 7. Câu hỏi phỏng vấn tiếp theo

- Linear probing vs quadratic probing vs double hashing — khác gì? Linear probing bị
  **primary clustering** thế nào?
- Robin Hood hashing là gì? Nó giảm variance của probe length ra sao?
- Vì sao `absl::flat_hash_map` dùng SIMD để probe 16 slot cùng lúc? (SSE, so 16 byte
  metadata một lần)
- Hash function tệ (nhiều va chạm) làm O(1) thoái hoá thành gì? Cách phòng?
- Vì sao `std::unordered_map::iterator` không invalidate khi insert, mà
  `vector::iterator` thì có? (node-based → reference stability, chính cái làm nó chậm)
