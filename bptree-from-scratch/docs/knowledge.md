# B+Tree từ Scratch — Knowledge Base

> Tài liệu này ghi lại kiến thức học được trong quá trình implement B+Tree.  
> Mục đích: ôn tập cho system design / database internals interview.

---

## Milestone 1 — Common Infrastructure

### Tại sao B+Tree dùng Page ID thay vì pointer?

Textbook B+Tree dùng raw pointer:
```cpp
Node* left_child = new Node();   // xấu
```

Storage-engine B+Tree dùng page ID:
```cpp
page_id_t left_child_id = 5;    // tốt
```

**Lý do:**
- Pointer chỉ có nghĩa trong một process đang chạy — restart là mất
- Page ID là địa chỉ logic trên disk — persist được
- Buffer pool dùng page ID để map vào đúng frame trong RAM

---

### `page_id_t` vs `frame_id_t`

| | `page_id_t` | `frame_id_t` |
|---|---|---|
| **Ý nghĩa** | Địa chỉ page trên **disk** | Slot trong **buffer pool** (RAM) |
| **Thời gian sống** | Vĩnh viễn | Tạm thời (thay đổi khi evict) |
| **Ví dụ** | Page 5 luôn là page 5 trên disk | Frame 2 hôm nay chứa page 5, ngày mai có thể chứa page 99 |

---

### Sentinel Value

Giá trị đặc biệt mang nghĩa "không có" — không phải dữ liệu thật.

```cpp
constexpr page_id_t INVALID_PAGE_ID = std::numeric_limits<page_id_t>::max();
// = 4,294,967,295 = 0xFFFFFFFF
```

Xuất hiện ở 3 chỗ quan trọng:
```cpp
parent_page_id_ == INVALID_PAGE_ID  → node này là ROOT
next_page_id_   == INVALID_PAGE_ID  → leaf này là CUỐI CÙNG
root_page_id_   == INVALID_PAGE_ID  → tree đang RỖNG
```

**Tại sao không dùng 0 hay -1?**
- `0` là page ID hợp lệ đầu tiên
- `-1` không dùng được với `uint32_t` (unsigned, không có số âm)
- `UINT32_MAX` không bao giờ xuất hiện thực tế (= 16TB database)

---

### PAGE_SIZE = 4096

Tại sao 4KB?
- Là kích thước 1 block của hầu hết ổ cứng / SSD
- `pread(fd, buf, 4096, offset)` chỉ tốn **1 disk I/O**
- Nếu dùng kích thước lẻ → OS phải đọc nhiều block, tốn kém

InnoDB mặc định: 16KB. PostgreSQL: 8KB. SQLite: 4KB.

---

### Comparator Pattern

B+Tree cần so sánh key nhưng không biết key là type gì → dùng **functor template**:

```cpp
template <typename Key>
struct DefaultComparator {
    int operator()(const Key& a, const Key& b) const noexcept;
    // Trả về: âm / 0 / dương  (giống strcmp)
};
```

Convention âm/0/dương cho phép dùng trực tiếp trong binary search:
```cpp
int cmp = comparator(search_key, node_key);
if (cmp < 0) go_left();
if (cmp > 0) go_right();
if (cmp == 0) found();
```

**Template Specialization:** định nghĩa behavior khác nhau cho một type cụ thể trong khi giữ nguyên template gốc cho các type khác.

```cpp
template <>
struct DefaultComparator<std::string> { ... };  // override riêng cho string
```

---

## Interview Questions — Milestone 1

**Q: Tại sao storage engine dùng page ID thay vì pointer?**
> Pointer chỉ valid trong memory của process hiện tại. Page ID là địa chỉ logic trên disk, persist qua restart. Buffer pool map page ID → frame ID tại runtime.

**Q: PAGE_SIZE = 4096 có ý nghĩa gì?**
> Align với block size của OS/disk. Mỗi read/write page tốn đúng 1 disk I/O. Misalignment → amplification (đọc nhiều block hơn cần thiết).

**Q: Comparator trong B+Tree dùng để làm gì?**
> Binary search bên trong node. Trả về âm/0/dương để code tìm kiếm không cần biết type của key.
