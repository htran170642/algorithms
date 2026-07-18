# W9 — Object Lifetime & UB

> Mục **5** để trống — tự viết.

---

## 0. Bằng chứng — ASAN kể toàn bộ câu chuyện

```
ERROR: heap-use-after-free ... READ of size 4
  #0 main dangle.cpp:14          ← NƠI ĐỌC bộ nhớ đã chết (printf elem)

freed by thread T0 here:
  #5 vector::_M_realloc_insert    ← NƠI GIẢI PHÓNG (realloc free buffer cũ)
  #7 main dangle.cpp:12          ← vòng push_back của bạn
```

ASAN cho **HAI** stack trace: nơi đọc + nơi free. Ghép lại ra ngay nguyên nhân.
Không có ASAN, chương trình có thể **in ra 10 bình thường** (bộ nhớ chưa bị ghi đè) →
bug sống tới production.

---

## 1. Dangling reference

```cpp
const std::string& bad() {
    std::string local = "hello";
    return local;              // trả reference tới BIẾN LOCAL
}                              // local chết ở đây → reference DANGLING
```

Đọc reference đó = **UB**. Đôi khi "may mà chạy" (chưa bị ghi đè) — kiểu UB nguy hiểm
nhất, vì nó im lặng *cho tới khi không*.

`-Wreturn-local-addr` bắt case rõ ràng; qua nhiều lớp hàm thì chỉ ASAN thấy.

---

## 2. Temporary lifetime — quy tắc & bẫy

Temporary bình thường chết ở **cuối câu lệnh** (`;`).

**Lifetime extension:** gán temporary cho `const&` (hoặc `&&`) → temporary sống theo
tuổi của reference:
```cpp
const std::string& r = std::string("hi");   // HỢP LỆ — r giữ temporary sống
```

**⚠️ Bẫy — extension KHÔNG bắc cầu:**
```cpp
const std::string& s = std::string("hello").substr(0, 3);
//                     └ temp gốc ┘         └ trả temp MỚI ┘
```
`substr` trả temporary **mới**; extension chỉ áp cho temporary **được gán trực tiếp**,
không áp cho temporary trung gian. `string("hello")` gốc chết ngay `;` → `s` dangling.

Trắc nghiệm đã trả lời đúng (A):
```cpp
const std::string& r = std::string("hello");             // AN TOÀN
const std::string& s = std::string("hello").substr(0,3); // DANGLING
```

Cách đúng: nhận **by value** (`std::string owned = ...substr(...)`) → sở hữu bản sao.
Bẫy này cực hay gặp với `string_view` → **W18**.

---

## 3. `[[nodiscard]]`

```cpp
[[nodiscard]] bool tryConnect();
tryConnect();     // -Werror=unused-result → build đỏ
```

Dùng cho hàm mà **bỏ qua kết quả là bug**: mã lỗi, `unique_ptr` từ factory, `empty()`
(hay bị nhầm là "xoá"). Với `-Werror`, quên xử lý = fail build.

---

## 4. Ai bắt loại UB nào

| Loại UB | Công cụ |
|---|---|
| use-after-free, dangling vào heap | **ASAN** ✅ (đã thấy) |
| signed overflow, oob shift | **UBSAN** ✅ (Week 0) |
| biến chưa khởi tạo | MSan / Valgrind (ASAN **không** bắt) |
| data race | **TSAN** (W32+) |
| iterator invalidation | ASAN (là use-after-free) → W12 |

> Sanitizer không phải thuốc tiên. Biết **công cụ nào bắt lỗi gì** là một phần của nghề.

---

## 5. Vì sao "may mà chạy" là kiểu UB nguy hiểm nhất — **TỰ VIẾT**

Gợi ý: tại sao một dangling read *đôi khi* in ra đúng giá trị? Điều đó khiến bug khó
phát hiện hơn hay dễ hơn? Vì sao chạy test dưới ASAN ở **mọi** tuần lại quan trọng?

**Trả lời:**




---

## 6. Câu hỏi phỏng vấn tiếp theo

- C++23 có `-Wdangling-reference`. Nó bắt được `substr` case ở mục 2 không? Thử.
- `std::string_view sv = std::string("hi");` — an toàn hay dangling? Vì sao? (→ W18)
- Lifetime extension có áp dụng khi temporary được gán cho **member reference** trong
  constructor không? *(gợi ý: không! đây là bẫy kinh điển)*
- Vì sao trả về `std::string` **by value** thì không dangling, dù `local` cũng chết?
  (→ nhớ RVO/move ở W1, W43)
- `for (auto x : getVector())` vs `for (auto& x : getTempObject().items())` — cái nào
  dangling? *(range-based for + temporary = bẫy hay gặp)*