# W25 — CRTP & Static Polymorphism (đấu virtual)

> Artifact: [`include/w25/shapes.hpp`](../include/w25/shapes.hpp) · Test: [`tests/crtp_test.cpp`](../tests/crtp_test.cpp) · Bench: [`bench/bench_crtp.cpp`](../../bench/bench_crtp.cpp)
> Mục **7** để trống — tự viết.

---

## 0. Bằng chứng

```
ALL GREEN — debug/asan/ubsan × C++20 và C++23
release-23 bench:  BM_VirtualDispatch  32714 ns   |   BM_CrtpStatic  7964 ns   → CRTP nhanh ~4.1×
```
Con số này là **tổng chi phí hai lối viết**, không phải mình dispatch (mục 4).

---

## 1. CRTP là gì — hình dạng

Base là template nhận **chính lớp con** làm tham số; con kế thừa base-đã-gắn-tên-mình:
```cpp
template <class Derived> class ShapeS {
    const Derived& self() const { return static_cast<const Derived&>(*this); }
public:
    double area() const { return self().area_impl(); }   // gọi TĨNH xuống con
};
class CircleS : public ShapeS<CircleS> { double area_impl() const {...}; };
```
`static_cast` xuống **an toàn vì** `CircleS` thật sự kế thừa `ShapeS<CircleS>`. Không có vtable — lời gọi lộ thiên lúc biên dịch nên compiler **inline** được.

---

## 2. ⭐ Lý do thật sự CRTP tồn tại: **static interface**, không phải tốc độ

Base viết logic tái dùng **một lần**, dựa trên primitive mỗi con cung cấp:
```cpp
double scaled_area(double k) const { return k * k * area(); }   // viết 1 lần cho MỌI shape
```
Đây là mẫu của `std::enable_shared_from_this`, `std::ranges::view_interface`, `std::totally_ordered` (qua `<=>`). Chúng dùng CRTP vì **giao diện**, không vì nanosecond. Tốc độ là phần thưởng.

---

## 3. Layout: CRTP không có vptr (nửa còn lại của câu chuyện tốc độ)

Base rỗng (chỉ hàm, không dữ liệu) → **EBO** (Empty Base Optimization) nuốt nó → object CRTP chỉ bằng dữ liệu của nó:
```cpp
static_assert(sizeof(CircleS) == sizeof(double));   // 8 — không vptr
static_assert(sizeof(Circle)  >  sizeof(double));    // 16 — có vptr
```
Không vptr nghĩa là: object nhỏ hơn, xếp **liền mạch** trong `vector<CircleS>`, cache thân thiện. Virtual bắt buộc con trỏ base → heap + rải rác.

---

## 4. Benchmark nói gì (và KHÔNG nói gì)

`32714 ns` vs `7964 ns` là **tổng gói chi phí** của virtual: gián tiếp qua vtable **+** pointer chasing (`vector<unique_ptr<Shape>>`) **+** mất inline. Không tách được một sợi, vì virtual **bắt buộc** con trỏ base — muốn đa hình runtime thì phải trả cả gói. Đó đúng là cái bạn trả trong code thật.

> Caveat (nợ Week 0): CPU frequency scaling còn bật → ns tuyệt đối nhiễu tới W41. **Tỉ số ~4×** mới là điều rút ra, không phải con số lẻ.

Mất inline là phần đắt nhất: compiler không thấy thân `area()` nên không gấp hằng, không vector hoá, không dọn vòng.

---

## 5. ⭐ Cái GÌ mất khi bỏ virtual (quiz Q4)

`ShapeS<CircleS>` và `ShapeS<SquareS>` là **hai kiểu không liên quan** — không có base chung để trỏ:
```cpp
static_assert(!std::is_same_v<ShapeS<CircleS>, ShapeS<SquareS>>);
```
→ **không nhét chung `vector<ShapeS*>` được.** CRTP là đa hình **đồng nhất** (biết kiểu lúc biên dịch). Cần chứa lẫn lộn nhiều kiểu quyết định lúc runtime → **virtual thắng**, hoặc `std::variant` + `visit` (đa hình "đóng" tĩnh).

---

## 6. Cạm bẫy: `static_cast` xuống là UB âm thầm nếu truyền nhầm lớp

`struct Bug : ShapeS<Other>` (truyền lớp khác vào tham số) → `static_cast<Other&>(*this)` trên một object thật là `Bug` = **UB, không cảnh báo mặc định**. Muốn chặn: ràng `requires std::derived_from<Derived, ShapeS<Derived>>` — nhưng **không đặt trên class template được** (lúc đó `Derived` chưa hoàn chỉnh), phải đặt trên **hàm thành viên**. Điểm nối thẳng vào concept W24.

---

## 7. TỰ VIẾT (retrieval — tự điền, không nhìn phía trên)

> Đề: **Benchmark cho CRTP nhanh ~4×. Nêu ba nguồn chi phí mà lối virtual trả còn CRTP thì không, và giải thích vì sao không thể tách riêng "chi phí vtable" ra đo một mình trong so sánh công bằng này.** 4–5 câu.

_(câu trả lời của bạn ở đây)_

---

## 8. Câu hỏi phỏng vấn nối tiếp

- CRTP nhanh hơn, vậy sao STL vẫn đầy virtual (vd `std::pmr::memory_resource`)? Khi nào runtime polymorphism là **đúng**, không phải "chưa tối ưu"?
- EBO là gì, và nếu `ShapeS` có **một** thành viên dữ liệu thì `sizeof(CircleS)` đổi thế nào?
- `std::variant` + `visit` cũng là đa hình tĩnh cho nhiều kiểu. So với CRTP và với virtual, nó nằm ở đâu về tốc độ và tính mở rộng?
- Vì sao `requires std::derived_from<D, ShapeS<D>>` không đặt được trên class template mà phải trên member function?
- "Deducing this" (C++23) thay được CRTP trong trường hợp nào? Viết `area()` bằng `this auto&& self` xem mất gì.
- Virtual gọi qua con trỏ base có thể được **devirtualize** khi nào? (gợi ý: `final`, kiểu tĩnh biết chắc) — lúc đó khoảng cách với CRTP còn bao nhiêu?
