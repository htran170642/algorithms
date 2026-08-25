# W1 — No-heap discipline · `FixedVector<T, Capacity>`

**Credit:** RAII, Rule of 0/3/5, move semantics, smart pointers, pool allocator, alignment/padding —
đã chứng minh ở `cplus_review` W1–W9, W19, W27–W31. Tuần này chỉ làm phần **delta automotive**: tại
sao code ASIL cấm `new`, và một container không đụng heap trông thế nào.

---

## 0. Bằng chứng

Artifact: `libs/av_core/include/av/fixed_vector.hpp` + `tests/fixed_vector_test.cpp`.
Xanh 6/6 preset: `debug-17`, `asan-17`, `ubsan-17`, `debug-20`, `asan-20`, `ubsan-20`.

| Đo | Kết quả | Chốt |
|---|---|---|
| `sizeof(std::vector<int>)` | **24 bytes** — bất kể chứa bao nhiêu | 3 con trỏ (begin/end/cap); payload nằm trên heap |
| `sizeof(FixedVector<int,8>)` | **40 bytes** (32 payload + 8 overhead) | ⭐ payload nằm **trong** object |
| `sizeof(FixedVector<int,1024>)` | **4104 bytes kể cả khi rỗng** | ⭐ giá phải trả: RAM tốn ngay từ lúc khai báo |
| `alignof(FixedVector<double,4>)` | **8** = `alignof(double)` | `alignas(T)` truyền đúng alignment lên cả object |
| `&v[0]` vs `[&v, &v+sizeof(v))` | nằm **bên trong** | ⭐ bằng chứng no-heap kiểu **structural**, không cần đếm `malloc` |
| `MoveIsLinearNotConstant` | `Tracker::moves == 3` khi move vector 3 phần tử | ⭐ move là **O(n)**, không phải O(1) |
| `ThrowingCopyLeavesNothingBehind` | `Bomb::live` về đúng 4, ASan im lặng | rollback thủ công trong `copy_from` là **bắt buộc** |

⭐ **Chốt đo được:** `std::vector` đổi *một indirection* lấy *kích thước object hằng số và move O(1)*.
`FixedVector` đổi ngược lại: **không indirection, không heap** — trả giá bằng *sizeof cố định* và
*move O(n)*. Không cái nào "tốt hơn"; chúng ở hai đầu của cùng một trade-off.

---

## 1. Tại sao ASIL cấm `new` — 5 lý do ⭐

Đây là câu hỏi phỏng vấn. Trả lời "vì nó chậm" là **sai** — đó là lý do yếu nhất và cũng không phải
lý do thật.

1. **WCET không bị chặn.** `malloc` có thể duyệt free list, lấy lock (đa luồng), hoặc gọi `mmap` —
   tức **syscall**. Không tính được worst case. Hard real-time cần *chặn trên chứng minh được*, chứ
   không phải "thường thì nhanh".
2. **Fragmentation qua 15 năm không reboot.** External fragmentation → `malloc(1KB)` fail *dù tổng
   trống còn nhiều*, vì không có khối liền nhau. Xe không có "restart service lúc 3h sáng".
3. **Failure mode không có câu trả lời tốt.** Hết bộ nhớ → `bad_alloc`, nhưng AUTOSAR C++14 hạn chế
   exception nặng. Không exception thì trả gì? `abort()` **không phải safe state** ở 100 km/h.
4. **Không analyzable.** ISO 26262 đòi *chứng minh* bộ nhớ có chặn trên. Static → `size <binary>` là
   xong. Heap → không chứng minh được.
5. ⭐ **Freedom from interference.** Từ khoá ISO 26262 quan trọng nhất ở đây. Component QM và
   component ASIL-D **dùng chung heap** → cái QM leak sẽ làm cái ASIL-D chết đói ⇒ **chúng không còn
   độc lập**. Static allocation tách bộ nhớ vật lý ⇒ mới chứng minh được không can thiệp.

Liên quan: AUTOSAR C++14 **A18-5-1..5** (không `malloc`/`free`, dynamic memory chỉ qua wrapper cho
phép), **A5-2-4** (cấm `reinterpret_cast`).

---

## 2. Quyết định thiết kế

### 2.1 Raw bytes, KHÔNG phải `T data_[Capacity]`

```cpp
alignas(T) std::byte storage_[Capacity * sizeof(T)];
```

`T data_[Capacity]` sẽ **default-construct cả N phần tử ngay lúc tạo**:
- bắt `T` phải default-constructible → `Tracker` trong test **không** có default ctor, và nó vẫn chạy
  được ⇒ đó chính là bằng chứng cho lựa chọn này;
- làm việc không ai yêu cầu (dựng 64 `Signal` lúc boot).

Raw bytes + **placement new** ⇒ phần tử chỉ tồn tại khi `push_back`.

### 2.2 Truy cập storage: `static_cast` qua `void*`, KHÔNG `reinterpret_cast`

```cpp
return std::launder(static_cast<T*>(static_cast<void*>(storage_)));
```

**AUTOSAR C++14 A5-2-4 cấm `reinterpret_cast`.** Hai `static_cast` qua `void*` là idiom hợp chuẩn, và
tiện thể tránh luôn `-Wcast-align`.

`std::launder` (C++17): nói với compiler *"ở địa chỉ này giờ có object thật rồi"*. Thiếu nó thì việc
đọc qua con trỏ dẫn xuất từ storage cũ là **UB về mặt lý thuyết** (compiler được phép giữ giả định cũ).

### 2.3 API đôi — `try_push_back` vs `push_back`

| | Trả về | Dùng khi |
|---|---|---|
| `try_push_back` | `bool`, `[[nodiscard]]` | **đường automotive** — tràn là một *giá trị* phải xử lý |
| `push_back` | `void`, throw `length_error` | đường tiện lợi, cho test và code không critical |

`[[nodiscard]]` làm việc bỏ qua kết quả trở thành **lỗi compile**, không phải bug im lặng.

### 2.4 `operator[]` vs `at()`

- `operator[]` mang **precondition** — `assert` ở debug, biến mất ở release. Dùng trong vòng lặp nóng
  đã chứng minh được bound.
- `at()` mang **check** — luôn kiểm tra, throw. Dùng ở **trust boundary** (dữ liệu từ CAN, từ IPC).

Đây là contract khác nhau, không phải "hai cách làm cùng một việc".

---

## 3. Exception safety — cái bẫy thật ⭐

```cpp
template <typename It>
void copy_from(It first, It last) {
  try {
    for (It it = first; it != last; ++it) { construct_at_end(*it); }
  } catch (...) {
    clear();      // ⭐ bắt buộc
    throw;
  }
}
```

**Tại sao bắt buộc:** nếu copy ctor của `T` ném ở phần tử thứ 3, thì `FixedVector` **vẫn đang trong
quá trình construct** ⇒ **destructor của nó SẼ KHÔNG chạy** (ngôn ngữ chỉ hủy object đã construct
xong). Hai phần tử đã dựng sẽ leak. Phải dọn tay rồi ném lại.

Test `ThrowingCopyLeavesNothingBehind` bắt đúng chuyện này; ASan cũng sẽ tố nếu quên.

⭐ Nếu `T` có copy `noexcept` thì `try/catch` **tốn 0 runtime** (zero-cost EH: chỉ có bảng unwind,
không có lệnh nào ở happy path).

---

## 4. Complexity & Invariants

| Thao tác | Complexity |
|---|---|
| `push_back` / `pop_back` / `operator[]` | O(1) |
| `clear` / dtor | O(n) |
| copy ctor | O(n) |
| **move ctor** | ⭐ **O(n)** — không phải O(1) như `std::vector` |

**Invariants:**
- `0 <= size_ <= Capacity` luôn đúng
- đúng `size_` object được construct trong `storage_`, ở vị trí `[0, size_)`
- phần tử được hủy **back-to-front** (giống thứ tự hủy của automatic storage)

---

## 5. Khi nào KHÔNG nên dùng ⭐

1. **Capacity nằm trong type.** `FixedVector<int,8>` và `FixedVector<int,16>` là **hai type không
   liên quan**. Một hàm nhận cả hai phải template hoá hoặc nhận span. `std::vector<int>` chỉ có một
   type. → Đây là lý do **W3 nên có một `Span<T>`** để làm interface chung.
2. **Tốn RAM kể cả khi rỗng.** Đo được: `FixedVector<int,1024>` = 4104 bytes ngay lúc khai báo. 100
   object như vậy = 400 KB, dùng hay không cũng mất.
3. ⭐ **Move O(n).** Không có con trỏ nào để ăn cắp, vì storage nằm *trong* object. ⇒ **không truyền
   by value**, không để trong `std::vector` mà resize nhiều.
4. **Không biết trước capacity.** Nếu số phần tử phụ thuộc input runtime không chặn được, fixed
   capacity chỉ đang giấu vấn đề — bạn sẽ mất dữ liệu im lặng ở `try_push_back` trả `false`.

---

## 6. Câu hỏi phỏng vấn (→ `docs/interview/`)

1. Tại sao code ASIL cấm dynamic allocation? *(kể được ≥3/5 lý do; nếu chỉ nói "chậm" là trượt)*
2. "Freedom from interference" nghĩa là gì, và heap phá nó thế nào?
3. Tại sao move của container storage-inline **không thể** là O(1)?
4. `std::launder` giải quyết vấn đề gì? Bỏ nó đi thì sai ở đâu?
5. `operator[]` và `at()` khác nhau ở **contract** nào, chứ không phải ở implementation?
6. Copy ctor ném exception giữa chừng — tại sao destructor của object đang construct **không** chạy,
   và hệ quả là gì?
7. Tại sao dùng raw `std::byte` storage thay vì `T data_[N]`?
8. Khi nào `std::vector` **tốt hơn** `FixedVector`?

---

## 7. Bẫy đã dính (ghi lại để khỏi mất thời gian lần sau)

⭐ **Preprocessor không hiểu template.**

```cpp
EXPECT_EQ(av::FixedVector<int, 4>::capacity(), 4U);
// error: macro "EXPECT_EQ" passed 3 arguments, but takes just 2
```

Preprocessor chạy **trước** parser, nên dấu phẩy trong `<int, 4>` bị coi là dấu ngăn tham số macro.
Cách chữa: `using Vec4 = av::FixedVector<int,4>;` rồi mới dùng trong macro (đọc dễ hơn là bọc thêm
một lớp ngoặc).

---

## 7b. `FixedString<N>` — tại sao KHÔNG phải `FixedVector<char, N>`

| Đo | Kết quả | Chốt |
|---|---|---|
| `sizeof(std::string)` | 32 B (SSO ~15 ký tự rồi ra heap) | libstdc++: ptr + size + union{cap, buf[16]} |
| `sizeof(FixedString<8>)` | **24 B**, không phải 17 | ⭐ **padding**: `char buf_[9]` bị đệm lên 16 để `size_t size_` đúng biên 8 |
| `sizeof(FixedString<255>)` | 264 B | 256 (255+NUL) + 8 |
| `strlen(c_str()) == size()` sau **mọi** mutation | luôn đúng | ⭐ invariant `buf_[size_] == '\0'` |
| `try_append` khi không vừa | buffer **không đổi** | all-or-nothing |
| `append_truncating` khi tràn | ghi phần vừa, trả **`false`** | cắt nhưng **không im lặng** |

### Ba lý do string cần class riêng ⭐

1. **NUL termination.** C API cần `const char*`: `open()`, `syslog()`, tên DTC, CAN tooling.
   `FixedVector<char,N>` không đảm bảo có `'\0'`. ⇒ buffer là `Capacity + 1` byte;
   **`Capacity` đếm KÝ TỰ, `sizeof(buf_)` đếm BYTE** — chỗ này sinh ra off-by-one.
2. **Truncation là khái niệm chỉ string mới có.** Nối 100 ký tự vào buffer 32: đúng với *log
   message*, là **bug nghiêm trọng** với *đường dẫn thiết bị* (`/dev/ttyUSB0` → `/dev/tty`).
   ⇒ chính sách cắt phải là **quyết định API**, không phải mặc định:
   - `try_append` — **toàn bộ hoặc không gì**; thất bại thì buffer nguyên vẹn → path, ID, tên signal
   - `append_truncating` — ghi phần vừa, **trả `false` nếu có cắt** → log message

   ⭐ **Cắt im lặng mới là bug.** Hoặc báo lỗi, hoặc báo đã cắt.
3. **Đây là nơi C sinh nhiều CVE nhất.**

### ⭐ Bẫy `strncpy` — câu hỏi phỏng vấn C kinh điển

```c
char dst[8];
strncpy(dst, "hello world", 8);   // dst = "hello wo" — KHÔNG có '\0'
printf("%s", dst);                 // đọc tràn ra ngoài buffer
```

> **`strncpy` KHÔNG NUL-terminate khi bị cắt.** Nó không phải "strcpy an toàn" — nó là hàm *điền
> buffer cố định* của Unix cổ (dùng cho record độ dài cố định như `utmp`). MISRA cấm `strcpy`/`strcat`
> vì không chặn; nhưng `strncpy` nguy hiểm hơn vì *trông có vẻ an toàn*.

`FixedString` tồn tại để failure mode đó **không thể xảy ra**.

### Chi tiết thiết kế đáng nhớ

- **Rule of Zero áp dụng được.** Buffer là `char[]` trivially copyable ⇒ copy/move do compiler sinh
  **vừa đúng vừa giữ invariant** (terminator nằm trong đám byte được copy). Không tự viết cái compiler
  làm đúng.
- **`memcpy`, không phải `strncpy`.** Nguồn là `string_view` — tự mang độ dài, không cần NUL. Và
  `strncpy` sẽ dừng sớm ở `'\0'` nhúng giữa chuỗi, sai khi payload hợp lệ có chứa `'\0'`.
- ⭐ **Hidden friend idiom.** `operator==` định nghĩa **bên trong** class template ⇒ với mỗi
  instantiation nó là hàm **thường**, không phải template ⇒ **implicit conversion mới kích hoạt** nên
  `fs == "abc"` chạy được. Nếu viết free function template thì deduction từ `const char[4]` sang
  `string_view` sẽ **thất bại**.

---

## 8. Nối vào capstone

`FixedVector` sẽ là container mặc định của:
- **W4** `av_signal` — bảng vehicle signal (số signal biết trước, cố định)
- **W9** `av_can` — payload CAN tối đa 8 byte (Classic) / 64 byte (FD) ⇒ fixed capacity là *đúng bản
  chất*, không phải là hạn chế
- **W11** `av_uds` — danh sách DTC có chặn trên theo thiết kế
