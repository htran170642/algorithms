# Milestone 3 — BPlusTreePage

## Vai trò của BPlusTreePage

`BPlusTreePage` định nghĩa **20 bytes đầu tiên** của mọi node trong B+Tree. Cả LeafPage và InternalPage đều bắt đầu bằng header này.

```
Page.data_[4096 bytes]:
[BPlusTreePage header: 20 bytes][... dữ liệu của LeafPage hoặc InternalPage ...]
```

---

## Memory Layout chính xác

```
offset  0: page_id_        (uint32_t, 4 bytes)
offset  4: parent_page_id_ (uint32_t, 4 bytes)
offset  8: size_           (uint32_t, 4 bytes)
offset 12: max_size_       (uint32_t, 4 bytes)
offset 16: is_leaf_        (bool,     1 byte)
offset 17: pad_[3]         (uint8_t,  3 bytes) ← explicit padding
total: 20 bytes
```

`static_assert(sizeof(BPlusTreePage) == 20)` enforce điều này tại compile time.

---

## Tại sao cần `pad_[3]`?

Compiler tự thêm padding để align data. Thay vì để ẩn, ta khai báo rõ:

- Document layout cho người đọc
- Tránh surprise khi đổi compiler/platform
- Khi thêm field mới → `static_assert` fail ngay → không bao giờ vô tình phá vỡ layout

---

## Quan hệ giữa Page và BPlusTreePage

Hai class **không kế thừa nhau** — liên quan qua memory:

| | `Page` | `BPlusTreePage` |
|--|--------|----------------|
| **Vai trò** | Vỏ bọc vật lý cho buffer pool | Header logic của B+Tree node |
| **Lưu ở đâu** | Object riêng trong buffer pool | 20 bytes đầu của `Page.data_` |
| **Persist ra disk** | ❌ (pin_count, is_dirty không ghi) | ✅ (toàn bộ data_ ghi ra disk) |
| **Ai dùng** | Buffer pool manager | B+Tree algorithms |

```cpp
Page* page = pool.FetchPage(id);
BPlusTreePage* header = page->As<BPlusTreePage>(); // không copy, chỉ reinterpret
```

---

## Các invariants quan trọng

```
IsFull()      ↔  size_ >= max_size_        → cần split
IsUnderflow() ↔  size_ <  max_size_ / 2   → cần borrow hoặc merge
IsRootPage()  ↔  parent_page_id_ == INVALID_PAGE_ID
```

Root page được miễn rule underflow — root có thể có ít entry hơn min_size.

---

## `size_` vs `max_size_`

- `max_size_` = số entries tối đa fit vào 1 page — **cố định**, tính từ `PAGE_SIZE` và `sizeof(entry)`
- `size_` = số entries hiện tại — **thay đổi** theo insert/delete

```
max_size_ cho LeafPage<int32_t, int32_t>:
= (4096 - 20 - 4) / sizeof(pair<int32_t,int32_t>)
= 4072 / 8
= 509 entries
```

---

## Interview Questions

**Q: Tại sao BPlusTreePage không có virtual destructor?**
> BPlusTreePage được tạo bằng `reinterpret_cast` từ raw buffer, không phải `new`. Destructor không bao giờ được gọi qua base pointer. Virtual destructor sẽ thêm 8-byte vptr → phá vỡ memory layout.

**Q: Sự khác nhau giữa `size_` trong BPlusTreePage và `sizeof()`?**
> `sizeof()` là kích thước compile-time của struct. `size_` là số entries runtime đang chứa trong node — thay đổi theo insert/delete.

**Q: Tại sao split xảy ra khi `size_ >= max_size_` chứ không phải `size_ > max_size_`?**
> Ta insert trước rồi check sau. Khi `size_ == max_size_` nghĩa là page đã đầy sau khi insert — cần split ngay. Không để page chứa hơn `max_size_` entries vì sẽ tràn ra ngoài 4096 bytes.
