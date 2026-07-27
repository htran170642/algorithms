# W29 — Arena / Bump Allocator: *tại sao* nó tồn tại

> Tuần khái niệm (chưa build artifact). Đồ chuẩn tương đương: `std::pmr::monotonic_buffer_resource`.
> Mục **6** để trống — tự viết.

---

## 0. Một câu

> Khi chương trình cấp phát **rất nhiều** object nhỏ trong một giai đoạn ngắn rồi **bỏ hết cùng lúc**, `new`/`delete` từng cái là nút thắt cổ chai. Arena biến hàng nghìn `malloc` + hàng nghìn `free` thành **vài phép cộng con trỏ** và **một lần xóa sạch**.

Không phải "cách khác cấp bộ nhớ cho vui" — là **tối ưu hiệu năng** cho một *pattern cấp phát* cụ thể.

---

## 1. ⭐ Mục đích: pattern "cùng vòng đời, chết cùng lúc"

`malloc` tổng quát phải phục vụ *mọi* pattern (cấp lúc nào cũng được, free lúc nào cũng được) → tốn phí: tìm block, ghi metadata per-block, có thể lock, block rải rác (cache lạnh).

Nhưng có một pattern cực phổ biến: **nhiều object nhỏ, cùng một vòng đời, chết một lượt.** Với nó, free lẻ từng cái là lãng phí — ta chỉ cần "quẹt một phát xóa sạch". Arena khai thác đúng điều đó.

**Ba tình huống thật:**

| Nơi | Object tạm | Khi nào bỏ hết |
|---|---|---|
| **Game engine** (mỗi frame ~16ms) | vật thể nhìn thấy, particle, draw command | cuối frame → `reset()` |
| **Server** (mỗi request) | header đã parse, object trung gian, kết quả | trả lời xong → `reset()` |
| **Parser / compiler** | hàng nghìn node cây AST | dùng xong cả cây → bỏ một lượt |

Nhận diện pattern là kỹ năng: thấy *"cấp nhiều thứ nhỏ rồi vứt tất cả cùng lúc"* → nghĩ arena. Thấy object **chết rải rác** lúc này lúc khác → arena **sai**, dùng `new`/`delete` hoặc pool (W30).

---

## 2. Cơ chế: allocate = MỘT phép cộng ("bump")

Arena xin HĐH **một khối lớn** một lần (vd 1MB), rồi tự chia bằng con trỏ `current`:

```
[############################.............................]
 ^begin              ^current                          ^end
 │←──── đã cấp ──────│←──────────── còn trống ─────────→│
```

```cpp
void* allocate(size_t n, size_t align) {
    current = align_up(current, align);   // đẩy lên bội align (xem W27!)
    void* p = current;
    current += n;                          // "bump" — hích con trỏ tới
    return p;                              // không tìm kiếm, không metadata, không lock
}
```

`align_up(addr, a) = (addr + (a-1)) & ~(a-1)` — làm tròn LÊN bội align gần nhất. Đây là điểm nối thẳng W27: object phải nằm đúng biên align, nếu không placement new của nó là UB.

---

## 3. ⭐ Giải phóng no-op — là TÍNH NĂNG, không phải lười

```cpp
void deallocate(void*, size_t) { /* không làm gì */ }   // đúng!
void reset() { current = begin; }                        // xóa sạch = một phép gán
```

Vì sao "không làm gì" lại đúng? Để trả một block *riêng lẻ*, bạn phải **theo dõi** nó — mà theo dõi chính là metadata + free-list đắt đỏ ta vừa vứt đi. Arena **mặc cả**: bỏ khả năng free lẻ → đổi lấy allocate cực nhanh + giải phóng cả khối tức thì.

**Giới hạn có chủ đích:** `reset()` **không** gọi `~T()` từng object. Nên arena hợp với **trivially-destructible** type (POD, số, struct thuần). Cấp 1000 `std::string` rồi chỉ `reset()` → **rò rỉ** buffer heap *bên trong* mỗi string (string chết mà destructor không chạy). Muốn dùng cho type non-trivial → phải tự lo destructor.

---

## 4. Vì sao nhanh — ba nguồn

1. **Allocate = 1 phép cộng** (vs `malloc` cả chục thao tác tìm block).
2. **Giải phóng = 1 phép gán** cho *toàn bộ* (vs hàng nghìn `free`).
3. **Cache locality**: object cấp liền mạch trong một khối → duyệt tuần tự rất nóng cache. Không metadata per-block → không phí RAM lẻ.

Đánh đổi: **mất linh hoạt (không free lẻ), được tốc độ.**

---

## 5. Cắm vào STL + `std::pmr`

**Allocator STL rút gọn (C++17):** `std::vector<T, A>` không đòi A đủ mọi thứ; `std::allocator_traits` điền hộ. Tối thiểu:
```cpp
template <typename T> struct ArenaAllocator {
    using value_type = T;
    T*   allocate(size_t n);        // -> arena.allocate(n*sizeof(T), alignof(T))
    void deallocate(T*, size_t) {}  // no-op
    // + operator==/!= (bằng nhau nếu chung arena) + ctor rebind từ U
};
```
`vector` gọi `allocate` lấy chỗ, rồi placement new từng phần tử (W28). → **W27 (align) + W28 (placement new) + W29 (arena) là một chuỗi liền.**

**`std::pmr` — chọn allocator lúc RUNTIME:** allocator cổ điển là *tham số template* → `vector<int, ArenaAllocator<int>>` và `vector<int>` là **hai kiểu khác nhau** (cứng nhắc). `pmr` biến allocator thành *con trỏ runtime* `memory_resource*`:
```cpp
std::pmr::monotonic_buffer_resource arena{1024*1024};  // ← ĐÂY là arena chuẩn
std::pmr::vector<int> v{&arena};                        // cùng kiểu dù resource nào
```
`monotonic_buffer_resource` = "con trỏ chỉ tiến, không lùi" = đúng bump allocator §2. **Chuẩn đã đóng gói sẵn** — hiểu ruột một lần, rồi dùng đồ chuẩn ("don't reinvent the wheel").

---

## 6. TỰ VIẾT (retrieval — tự điền, không nhìn phía trên)

> Đề: **Bạn thấy trong profiler rằng allocator là hotspot của một server xử lý request. Giải thích: (a) pattern cấp phát nào khiến arena phù hợp, (b) vì sao `deallocate` no-op lại đúng, (c) một loại object mà bạn KHÔNG được để arena `reset()` mà không xử lý thêm, và vì sao.** 4–5 câu.

_(câu trả lời của bạn ở đây)_

---

## 7. Câu hỏi phỏng vấn nối tiếp

- Arena vs **pool** allocator (W30) — khác nhau chỗ nào? (gợi ý: pool free lẻ được, arena thì không)
- Khi object trong arena là non-trivial destructible, có cách nào vẫn dùng arena an toàn? (gợi ý: giữ danh sách destructor, hoặc chỉ cho phép trivially-destructible qua concept)
- Arena đa luồng: `current += n` có an toàn không? Làm sao cho thread-safe mà không giết mất tốc độ? (gợi ý: atomic fetch_add, hoặc arena per-thread)
- `monotonic_buffer_resource` khi hết khối 1MB thì làm gì? (gợi ý: xin khối mới từ upstream resource)
- Vì sao HFT / game thích arena hơn GC hay `malloc`? Nói về **độ trễ đuôi** (tail latency), không chỉ throughput.
