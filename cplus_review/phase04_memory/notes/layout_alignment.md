# W27 — Layout · Alignment · Padding · `alignas`

> Artifact: [`include/w27/layout.hpp`](../include/w27/layout.hpp) · Test: [`tests/layout_test.cpp`](../tests/layout_test.cpp)
> Mục **7** để trống — tự viết.

---

## 0. Bằng chứng

```
ALL GREEN — debug/ubsan × C++20 và C++23. Toàn bộ khẳng định là static_assert:
layout là thuộc tính compile-time nên compiler mới là trọng tài, không phải EXPECT runtime.
```

---

## 1. Ba luật layout (tất cả suy ra từ một nhu cầu: đọc aligned)

1. **Internal padding** — mỗi thành viên đặt ở offset chia hết `alignof` của *chính nó*.
2. **Trailing padding** — `sizeof(struct)` làm tròn **lên** bội của `alignof(struct)` (= align lớn nhất trong các thành viên).
3. **Reorder** giảm-dần-theo-size xoá internal padding; trailing padding chỉ mất khi tổng đã vừa khít.

`alignof(struct)` = max của align các thành viên. Đây là mấu chốt của cả luật 1 lẫn 2.

---

## 2. ⭐ Tại sao trailing padding BẮT BUỘC (quiz Q3 — tôi từng chọn 10)

`struct Good { double b; char a; char c; };` dùng đúng 10 byte dữ liệu. Nhưng `sizeof == 16`, không phải 10. Vì sao không cắt xuống 10?

> **Vì mảng.** `Good arr[2]` cần `arr[1].b` cũng aligned-8. `&arr[1] = &arr[0] + sizeof(Good)`. Nếu `sizeof==10`, `arr[1]` bắt đầu ở offset 10 → `double b` của nó lệch khỏi bội-8 → **misaligned**.

Nên `sizeof` *phải* là bội của `alignof` để **stride** mảng giữ mọi phần tử aligned. 6 byte trailing padding là giá của "phần tử nào trong mảng cũng aligned". Reorder cứu được khe *giữa* (24→16), **không** xoá được đuôi.

```cpp
static_assert(sizeof(w27::Good) == 16);
static_assert(sizeof(w27::Good) != 10);   // trailing pad còn đó
static_assert(sizeof(w27::Good) < sizeof(w27::Bad));  // reorder là lời thuần
```

---

## 3. `sizeof(Bad) == 24` — đếm từng byte

```cpp
struct Bad { char a; double b; char c; };
//  a  @0                       (1 byte)
//     @1..7  ← 7 byte internal padding để b lên offset 8
//  b  @8..15                   (8 byte)
//  c  @16                      (1 byte)
//     @17..23 ← 7 byte trailing padding để sizeof chia hết 8
//  => 24
```
10 byte dữ liệu, 14 byte padding. Reorder `double` lên đầu: `Good` = 16 (10 data + 6 trailing). Chỉ đổi thứ tự khai báo, tiết kiệm 1/3 bộ nhớ, **zero cost**. Đây là tối ưu "miễn phí" đầu tiên trong Phase 4.

---

## 4. ⭐ `alignas` chỉ NỚI RỘNG · `[[no_unique_address]]` chỉ CO LẠI (quiz Q4)

Hai attribute đối xứng gương, cùng thao tác trên layout nhưng ngược chiều:

| | Hướng | Dùng để |
|---|---|---|
| `alignas(N)` | **tăng** align lên N (N ≥ align tự nhiên), `sizeof` phình theo | đẩy biến nóng lên **cache line** riêng → tránh **false sharing** (W31) |
| `[[no_unique_address]]` (C++20) | cho thành viên **rỗng** chồng chỗ → góp 0 byte | policy/comparator/allocator stateless không tốn size |

```cpp
struct alignas(64) CacheLinePadded { std::int64_t counter; };
static_assert(alignof(CacheLinePadded) == 64);
static_assert(sizeof(CacheLinePadded)  == 64);   // 1 int64 phình thành 1 line

struct WithAttr { [[no_unique_address]] Empty e; std::int32_t n; };
static_assert(sizeof(WithAttr) == 4);            // Empty chồng lên n, không góp byte
```

`alignas` **không bao giờ** giảm `sizeof` (quiz Q4 phương án C sai — nhân quả ngược: nó *thêm* padding). Và không quyết định stack/heap (phương án A sai) — chỉ quyết định địa chỉ chia hết bao nhiêu.

> Ghi chú thực chiến: dùng literal `64` chứ không `std::hardware_destructive_interference_size` vì GCC bắn `-Winterference-size` (cảnh báo ABI-stability) mà repo build warnings-as-errors.

---

## 5. Tại sao phần cứng đòi aligned

Bus đọc theo **word**. `double` ở địa chỉ chia hết 8 → nằm gọn trong 1 lần đọc. Lệch biên →
- x86-64: cần 2 lần đọc + ghép → **chậm** (vẫn chạy).
- ARM cũ, SIMD `movaps`, một số ISA: **fault / UB thẳng**.

Padding là cuộc đổi chác: tốn RAM để mua tốc độ + tính đúng đắn. Compiler không "thích" padding — nó bị ràng buộc phần cứng ép.

---

## 6. Stack vs Heap (quiz Q5 — đúng)

| | Cấp phát | Chi phí |
|---|---|---|
| **Stack** | dịch `rsp` một phép trừ | ~1 lệnh, không lock, không metadata, cache nóng |
| **Heap** | allocator tìm free block | có thể lock, có metadata header, có thể lỗi cache, có thể `mmap`/`brk` |

Stack nhanh hơn bậc độ lớn cho cấp phát/giải phóng, và dữ liệu stack liền mạch nên cache thân thiện. Đổi lại: kích thước giới hạn (~8MB), sống theo scope. Heap: sống lâu tuỳ ý, lớn tuỳ ý, trả bằng chi phí allocator + phân mảnh. Đây là nền cho W29 (arena) và W30 (pool): *tự* quản heap để lấy lại tốc độ kiểu-stack.

---

## 7. TỰ VIẾT (retrieval — tự điền, không nhìn phía trên)

> Đề: **`struct S { char a; double b; char c; };` cho `sizeof==24`. Giải thích từng byte offset, vì sao trailing padding tồn tại (dùng lập luận về mảng), reorder tối ưu ra `sizeof` bao nhiêu, và vì sao không xuống được 10.** 5–6 câu.

_(câu trả lời của bạn ở đây)_

---

## 8. Câu hỏi phỏng vấn nối tiếp

- `#pragma pack(1)` bỏ hết padding. Nêu **một** thứ có thể vỡ và **một** trường hợp bắt buộc dùng (gợi ý: giao thức mạng / file format on-disk).
- `alignof(std::max_align_t)` là gì, và vì sao `malloc` trả con trỏ aligned tới nó?
- Bit-field (`unsigned x : 3;`) ảnh hưởng layout thế nào? Thứ tự bit có portable không?
- `std::hardware_destructive_interference_size` vs `constructive` — khác nhau chỗ nào, khi nào cần cái sau?
- Struct có `virtual` thì vptr nằm đâu trong layout, và `offsetof` còn hợp lệ không? (gợi ý: standard-layout)
- Vì sao `[[no_unique_address]]` chỉ giúp với thành viên **rỗng**, không giúp với thành viên có dữ liệu?
