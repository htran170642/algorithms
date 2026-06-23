# Milestone 4 — LeafPage

## Vai trò

LeafPage là node lá của B+Tree — nơi thật sự lưu trữ dữ liệu `(key, value)`.  
Tất cả dữ liệu trong B+Tree đều nằm ở leaf level — internal nodes chỉ là "bảng chỉ đường".

---

## Memory Layout

```
Page.data_[4096 bytes]:
[BPlusTreePage header : 20 bytes]  ← kế thừa, is_leaf = true
[next_page_id_        :  4 bytes]  ← trỏ sang leaf kế tiếp
[pairs_[MAX_SIZE]     : còn lại]   ← mảng (key, value) sorted

MAX_SIZE = (4096 - 20 - 4) / sizeof(pair<K,V>)
         = 4072 / 8   (với int32_t key + int32_t value)
         = 509 entries
```

---

## Leaf Linked List

Tất cả leaf pages nối nhau thành một **linked list**:

```
Leaf 1 [1,3,5] → Leaf 2 [7,9,11] → Leaf 3 [13,15] → INVALID_PAGE_ID
```

Mục đích: **Range scan không cần đi qua internal nodes** — chỉ cần tìm leaf đầu tiên rồi đi theo `next_page_id_`.

---

## Tại sao dùng `static Init()` thay vì constructor?

```cpp
static void Init(LeafPage* self, page_id_t id, page_id_t parent) noexcept;
```

LeafPage được tạo bằng `reinterpret_cast` từ raw buffer — constructor không bao giờ được gọi. `Init()` thay thế constructor, nhận explicit `self` pointer.

---

## Các hàm quan trọng

### `Lookup` — Binary search O(log n)

Dùng `std::lower_bound` tìm vị trí đầu tiên có `key >= search_key`.  
Sau đó kiểm tra xem có bằng đúng không (lower_bound không đảm bảo equality).

```cpp
// Trả về std::nullopt nếu không tìm thấy — không throw exception
std::optional<Value> Lookup(const Key& key, const Cmp& cmp);
```

### `Insert` — Giữ sorted order

1. `lower_bound` tìm vị trí đúng
2. Reject duplicate (B+Tree không cho duplicate key)
3. `std::move_backward` dịch phải để tạo chỗ trống
4. Đặt entry mới vào

```
Trước: [1, 3, 7]   insert 5
Sau:   [1, 3, 5, 7]
```

### `Remove` — Dịch trái để lấp chỗ trống

1. `lower_bound` tìm vị trí
2. `std::move` dịch trái từ vị trí sau về đầu
3. `IncreaseSize(-1)`

```
Trước: [1, 3, 5, 7]   remove 3
Sau:   [1, 5, 7]
```

### `MoveHalfTo` — Dùng khi SPLIT

Chia đôi leaf khi đầy:
- `this` giữ nửa đầu `[0, half)`
- `recipient` nhận nửa sau `[half, size)`
- Cập nhật linked list: `this → recipient → old_next`
- Separator key đẩy lên parent = `recipient->EntryAt(0).first`

**Khác InternalPage split:** key vẫn còn trong recipient (không bị xóa).

### `MoveAllTo` — Dùng khi MERGE (coalesce)

Gộp `this` vào `recipient` (sibling trái):
- Copy toàn bộ entries vào cuối recipient
- `recipient` tiếp quản `next_page_id_` của `this`
- `this` bị xóa khỏi parent

---

## `std::move` vs `std::move_backward`

| Hàm | Hướng | Dùng khi |
|-----|-------|---------|
| `std::move(first, last, dest)` | Trái → Phải | Dịch trái (Remove) |
| `std::move_backward(first, last, dest)` | Phải → Trái | Dịch phải (Insert) |

Sai hướng → overwrite element chưa được copy → data corruption.

---

## Interview Questions

**Q: Tại sao leaf pages được nối nhau thành linked list?**
> Để range scan O(k) thay vì O(k log n). Sau khi tìm leaf đầu tiên bằng `FindLeafPage` (O(log n)), chỉ cần follow `next_page_id_` để lấy các key tiếp theo — không phải traverse lại từ root.

**Q: B+Tree có cho phép duplicate key không?**
> Không, trong implementation này. `Insert` trả về `false` khi gặp duplicate. Production database (MySQL) dùng composite key (key + row_id) để handle duplicate.

**Q: Tại sao separator key sau leaf split là `recipient->EntryAt(0).first`?**
> Đó là key nhỏ nhất của leaf mới — mọi key trong leaf mới đều >= separator. Key đó vẫn còn trong leaf (không bị xóa), nên internal node chỉ cần copy nó lên, không phải move.

**Q: Split xảy ra khi nào?**
> Sau khi insert, nếu `size_ >= MAX_SIZE` thì split. Ta insert trước rồi check — không check trước khi insert — để tránh phải handle edge case "insert vào đâu sau split".
