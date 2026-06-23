# Milestone 5 — InternalPage

## Vai trò

InternalPage là node trung gian của B+Tree — chứa **separator keys** và **child page IDs**.  
Không chứa value thật. Chỉ là "bảng chỉ đường" để tìm đúng LeafPage cần đến.

---

## Memory Layout

```
Page.data_[4096 bytes]:
[BPlusTreePage header : 20 bytes]   ← is_leaf = false
[array_[MAX_SIZE]     : còn lại]    ← mảng { key, child_page_id }

MAX_SIZE = (4096 - 20) / sizeof(pair<int32_t, page_id_t>)
         = 4076 / 8
         = 509 entries
```

---

## Convention array_[0] — Quan trọng nhất

Một InternalPage có `n` keys thì có `n+1` children.  
Vì mảng chỉ có 1 loại entry `{key, child}`, giải pháp là **bỏ qua array_[0].key**:

```
size_ = 3  →  3 entries (index 0, 1, 2)
            = 2 separator keys + 3 children

array_[0] = { IGNORED , child_0 }   ← leftmost child, không có separator
array_[1] = {    10   , child_1 }   ← key >= 10 → child_1
array_[2] = {    30   , child_2 }   ← key >= 30 → child_2
```

**Minh họa:**

```
Internal: [_, P0 | 10, P1 | 30, P2]
               ↓        ↓        ↓
           [1,3,5]  [10,20]  [30,40]

key < 10       → đi vào P0
10 <= key < 30 → đi vào P1
key >= 30      → đi vào P2
```

---

## Lookup — Binary Search

Tìm child phù hợp cho một key. Binary search trên `array_[1..size_-1]`.

```
array_: [_, P0 | 10, P1 | 30, P2]   size_ = 3

Lookup(key=15):
  lo=1, hi=3
  mid=2 → array_[2].key=30 > 15  → hi=2
  mid=1 → array_[1].key=10 ≤ 15  → lo=2
  lo==hi=2 → dừng
  return array_[1].child = P1  ✓

Lookup(key=5):
  mid=2 → 30 > 5  → hi=2
  mid=1 → 10 > 5  → hi=1
  lo==hi=1 → dừng
  return array_[0].child = P0  ✓  (leftmost)

Lookup(key=35):
  mid=2 → 30 ≤ 35 → lo=3
  lo==hi=3 → dừng
  return array_[2].child = P2  ✓
```

---

## InsertNodeAfter — Sau khi child split

Khi child page split, cần insert separator key mới vào InternalPage:

```
Trước:
array_: [_, P0 | 10, P1 | 30, P2]

Child P1 split → P1 (left) + P1_new (right), separator = 20
InsertNodeAfter(old_child=P1, key=20, new_child=P1_new):

  Tìm index của P1 → idx=1
  Shift phải từ idx+1
  array_[2] = {20, P1_new}

Sau:
array_: [_, P0 | 10, P1 | 20, P1_new | 30, P2]
```

---

## MoveHalfTo — Dùng khi SPLIT internal node

**Tình huống:** Internal node đầy sau khi nhận separator key từ child split.

```
Trước (size=5):
array_: [_, P0 | 10, P1 | 20, P2 | 30, P3 | 40, P4]
           [0]    [1]    [2]    [3]    [4]

half = 5/2 = 2
middle entry = array_[2] = {20, P2}
push_up_key  = 20  ← float lên parent, BỊ XÓA khỏi cả hai node
```

```
Sau MoveHalfTo(recipient, push_up_key):

this (left):
array_: [_, P0 | 10, P1]        size=2
           [0]    [1]

recipient (right):
array_: [_, P2 | 30, P3 | 40, P4]    size=3
           [0]    [1]    [2]
           ↑ array_[0].child = P2 (child của middle entry)
             array_[0].key = IGNORED

Parent nhận push_up_key = 20:
[... | 20 | ...]
        ↓      ↓
      this   recipient
```

**Điểm khác biệt với LeafPage:**
- LeafPage split: separator key **COPY** lên parent, vẫn còn trong right leaf
- InternalPage split: separator key **MOVE** lên parent, **BỊ XÓA** khỏi cả hai node

Lý do: internal key chỉ là separator, không phải data thật → relocate được.

---

## MoveAllTo — Dùng khi MERGE (coalesce)

**Tình huống:** Internal node underflow, không borrow được → merge với left sibling.

```
Trước merge:
parent: [_, P_left | 20, P_right | 50 | ...]
                     ↑ parent_key = 20

left:   [_, P0 | 10, P1]    size=2
right:  [_, P2 | 30, P3]    size=2  ← underflow, sẽ bị xóa
```

```
Bước 1: Kéo parent_key=20 xuống vào left
left:  [_, P0 | 10, P1 | 20, P2]
                         ↑ left[2].key  = parent_key = 20
                           left[2].child = right->array_[0].child = P2

Bước 2: Copy right[1..] vào left
left:  [_, P0 | 10, P1 | 20, P2 | 30, P3]   size=4

right: []  size=0  (bị parent xóa)

Parent xóa pointer đến right và separator 20:
parent: [_, P_left | 50 | ...]
```

**Tại sao phải kéo parent_key xuống?**

Khi merge 2 internal nodes, separator key trong parent phân chia chúng phải được đưa xuống. Sau merge chỉ còn 1 node, separator đó không còn ý nghĩa ở parent nữa → trở thành separator **bên trong** node đã merge.

---

## So sánh LeafPage vs InternalPage

| | LeafPage | InternalPage |
|--|---------|-------------|
| **Chứa** | (key, value) — dữ liệu thật | (key, child_id) — chỉ đường |
| **next_page_id_** | ✅ Có (linked list) | ❌ Không |
| **Split: key** | Copy lên parent, giữ trong right | Move lên parent, xóa khỏi cả hai |
| **Merge: parent_key** | Không cần | Phải kéo xuống |
| **Lookup trả về** | std::optional\<Value\> | page_id_t |

---

## Interview Questions

**Q: Tại sao array_[0].key bị bỏ qua trong InternalPage?**
> n keys cần n+1 children. Dùng mảng `{key, child}[n]` ta có n keys và n children. Để có thêm 1 child (leftmost), quy ước bỏ qua key của phần tử đầu tiên — chỉ dùng child của nó.

**Q: Tại sao InternalPage split phải xóa middle key, còn LeafPage thì không?**
> LeafPage lưu data thật — key không thể mất. InternalPage chỉ dùng key làm separator — khi key được đẩy lên parent, nó đã làm xong vai trò ở level này. Giữ lại sẽ tạo duplicate trong cây.

**Q: Điều gì xảy ra với children của right node sau khi merge internal nodes?**
> Tất cả children của right node trở thành children của left node sau khi merge. Parent key phải được kéo xuống để làm separator giữa các children đó trong node mới.
