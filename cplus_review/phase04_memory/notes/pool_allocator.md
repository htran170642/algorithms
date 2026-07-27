# W30 — Pool Allocator (free-list)

> Artifact: [`include/w30/pool.hpp`](../include/w30/pool.hpp) · Test: [`tests/pool_test.cpp`](../tests/pool_test.cpp) · Bench: [`bench/bench_pool.cpp`](../../bench/bench_pool.cpp)
> Mục **7** để trống — tự viết.

---

## 0. Bằng chứng

```
ALL GREEN — debug/asan/ubsan × C++20 và C++23 (union + reinterpret_cast + placement new sạch)
release-23 bench (1000 node/batch):  BM_NewDelete 13273 ns  |  BM_Pool 2055 ns  → pool nhanh ~6.5×
```

---

## 1. ⭐ Pool vá đúng điểm yếu của arena

Arena (W29) **không free lẻ được** — chỉ `reset()` cả khối. Pool sinh ra cho pattern ngược lại: **rất nhiều object CÙNG kích thước, vòng đời LỆCH nhau**, cấp/giải phóng liên tục.

| | Arena (W29) | Pool (W30) |
|---|---|---|
| Kích thước object | bất kỳ | **cố định** (một size) |
| Free lẻ | ❌ chỉ reset cả khối | ✅ O(1) qua free-list |
| Vòng đời | cùng lúc | **lệch nhau** |
| Đồ chuẩn | `monotonic_buffer_resource` | `unsynchronized_pool_resource` |

Ví dụ dùng pool: node của `list`/`map` (cùng size, thêm/xóa lẻ), đạn/enemy trong game (sống-chết độc lập), connection (W56).

---

## 2. ⭐ Free-list nằm TRONG chính slot đã chết

Chìa khóa: khi `destroy` một object, slot của nó **chưa trả về HĐH** — nó được móc vào **free-list** (danh sách liên kết các ô trống). Con trỏ "next" lưu ở đâu? **Ngay trong ô trống đó** — object đã chết, bộ nhớ đang rảnh, mượn luôn làm con trỏ. **Không tốn metadata riêng.**

```cpp
union Slot {
    alignas(T) std::byte storage[sizeof(T)];   // khi sống: chứa T
    Slot* next;                                 // khi chết: con trỏ free-list
};
```
`union` bảo đảm slot đủ lớn + đúng align cho **cả hai** vai. Freed slot host con trỏ next với **zero byte thừa**.

```cpp
T* create(...) {                       // O(1): pop free-list
    Slot* s = pop_free();              //   nếu rỗng → grow()
    return ::new (s) T(...);           //   placement new (W28)
}
void destroy(T* p) {                   // O(1): push free-list
    p->~T();
    push_free(reinterpret_cast<Slot*>(p));
}
```
LIFO (last freed = first reused) → ô vừa trả còn **nóng cache**.

---

## 3. Vì sao ~6.5× nhanh (13273 → 2055 ns)

`new`/`delete` qua `malloc` tổng quát: tìm block, ghi metadata per-block, có thể lock. Pool:
1. **create = free-list pop** (đọc `free_head_`, gán `free_head_ = s->next`) — vài lệnh, không tìm kiếm.
2. **destroy = free-list push** — vài lệnh.
3. **Cache locality**: slot cấp liền trong chunk + LIFO reuse → duyệt nóng cache.

`grow()` xin **một chunk lớn** (64 slot) một lần rồi thread hết vào free-list — biên chế `malloc` chia đều cho 64 lần cấp. Chunk **không bao giờ** bị move/free giữa chừng (`vector<unique_ptr<Slot[]>>` giữ) → con trỏ đã phát ra luôn hợp lệ (test `GrowsAcrossChunksAndKeepsPointersValid`).

---

## 4. Giới hạn có chủ đích (giống arena)

Pool **không** theo dõi slot nào còn object sống → `~ObjectPool` **không** gọi `~T()` cho phần còn sót. **Phải `destroy` hết trước khi pool chết**, nếu không destructor của object không chạy → rò tài nguyên bên trong (đúng bài học W28). Đổi lại: allocate/deallocate cực rẻ.

---

## 5. `std::pmr::unsynchronized_pool_resource`

Chuẩn C++17 đóng gói sẵn pool: `unsynchronized_pool_resource` (một luồng) / `synchronized_pool_resource` (đa luồng, có lock). Nó quản **nhiều size-class** (pool cho từng khoảng kích thước) chứ không chỉ một size như bản ta viết. Build một lần để thấy free-list, rồi dùng đồ chuẩn.

---

## 6. Cạm bẫy đã tránh

- **Slot phải đủ chỗ cho con trỏ**: `union` lo tự động (`sizeof(Slot) ≥ sizeof(Slot*)`), khỏi `static_assert` thủ công.
- **Align cho cả T lẫn con trỏ**: `alignas(T)` trên storage + `Slot*` member ép align lên đủ cho cả hai.
- **reinterpret_cast slot↔T**: hợp lệ vì union, cùng địa chỉ; ASAN/UBSAN xác nhận sạch.
- **Con trỏ treo khi grow**: chunk cũ không bị realloc (mỗi chunk là `unique_ptr` riêng, `vector` chỉ giữ con trỏ tới chúng).

---

## 7. TỰ VIẾT (retrieval — tự điền, không nhìn phía trên)

> Đề: **Giải thích cách một freed slot lưu con trỏ free-list mà KHÔNG tốn thêm byte nào, vì sao create/destroy là O(1), và một khác biệt bản chất giữa pool và arena quyết định khi nào bạn chọn cái nào.** 4–5 câu.

_(câu trả lời của bạn ở đây)_

---

## 8. Câu hỏi phỏng vấn nối tiếp

- Pool này chỉ một size-class. `pmr::unsynchronized_pool_resource` phục vụ nhiều size thế nào? (gợi ý: nhiều free-list theo khoảng size + upstream)
- Free-list LIFO vs FIFO — cái nào cache tốt hơn, vì sao?
- Làm pool thread-safe: lock-free free-list (CAS) gặp vấn đề **ABA** gì? (nối thẳng W39)
- Nếu object trong pool là non-trivially-destructible và bạn quên `destroy`, ASAN có bắt không? (gợi ý: giống test "orphan" W28 — không)
- Khi nào `new`/`delete` thực ra đã **đủ nhanh** và pool là tối ưu hóa sớm? Làm sao biết? (gợi ý: profile trước)
- `boost::pool` / `std::pmr` khác bản tự viết chỗ nào về an toàn và tính năng?
