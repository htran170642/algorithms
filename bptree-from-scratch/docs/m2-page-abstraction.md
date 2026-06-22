# Milestone 2 — Page Abstraction

## Page là gì trong storage engine?

`Page` là đơn vị I/O nhỏ nhất — mỗi lần đọc/ghi disk là đọc/ghi đúng 1 page (4096 bytes).

```
Disk layout:
[page 0: 4096 bytes][page 1: 4096 bytes][page 2: 4096 bytes]...

Offset của page N = N * 4096
```

---

## Các thành phần của Page

| Field | Kiểu | Mục đích |
|-------|------|---------|
| `data_` | `std::array<std::byte, 4096>` | Nội dung thật từ disk |
| `page_id_` | `page_id_t` | Frame này đang chứa page nào trên disk |
| `pin_count_` | `uint32_t` | Bao nhiêu operation đang dùng page này |
| `is_dirty_` | `bool` | RAM khác disk không? Cần write khi evict không? |

---

## Dirty Page Protocol

```
1. FetchPage(id)  → load từ disk vào RAM, pin_count++
2. Sửa data_      → đánh dấu is_dirty_ = true
3. UnpinPage(id)  → pin_count--
4. Khi evict:
   - is_dirty_ = false → bỏ thẳng (disk == RAM, không cần ghi)
   - is_dirty_ = true  → WritePage() ra disk trước, rồi mới evict
```

---

## Tại sao copy/move bị delete?

Buffer pool lưu `std::vector<Page> pages_`. Nếu vector resize → tất cả Page bị move sang vùng nhớ mới → mọi `Page*` pointer đang giữ đều **dangling** (trỏ vào vùng nhớ cũ đã freed).

**Giải pháp:** `reserve(pool_size)` từ đầu + delete copy/move → vector không bao giờ resize → địa chỉ ổn định mãi mãi.

---

## `As<T>()` — Type Punning

```cpp
template <typename T>
T* As() noexcept { return reinterpret_cast<T*>(data_.data()); }
```

Không tạo object mới, không copy — **nhìn cùng 1 vùng memory theo type khác**. Đây là kỹ thuật core của mọi storage engine:

```cpp
Page* page = pool.FetchPage(5);
page->As<LeafPage<int,int>>();   // nhìn 4096 bytes như LeafPage
page->As<InternalPage<int>>();   // nhìn 4096 bytes như InternalPage
```

---

## `std::byte` vs `char`

- `char*` — compiler có thể tối ưu sai vì strict aliasing rules
- `std::byte*` — C++17 explicitly cho phép alias bất kỳ type nào → `reinterpret_cast` an toàn

---

## `alignas(8)`

Đảm bảo `data_` bắt đầu ở địa chỉ chia hết cho 8.

Các type yêu cầu 8-byte alignment: `int64_t`, `double`, pointer (64-bit). Nếu sai alignment:
- x86: chạy được nhưng chậm hơn
- ARM: crash (bus error)

---

## Interview Questions

**Q: Page trong buffer pool khác Page trên disk như thế nào?**
> Page trên disk là dãy bytes tại offset cố định. Page trong buffer pool là cùng dãy bytes đó được load vào RAM, kèm metadata (pin_count, is_dirty) để buffer pool quản lý.

**Q: Tại sao cần pin_count thay vì chỉ cần 1 bool "đang dùng"?**
> Nhiều operation có thể đồng thời giữ page (ví dụ: traverse tree từ nhiều thread). `pin_count` đếm số lượng, chỉ evict khi = 0.

**Q: Dirty page được xử lý thế nào khi crash?**
> Trong project này không có WAL nên dirty page bị mất. Production database dùng Write-Ahead Log: ghi log trước, ghi page sau — recovery replay log sau crash.
