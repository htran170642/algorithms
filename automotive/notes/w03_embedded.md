# W3 — Embedded C++

Phần 1: `volatile`. *(bit / endian / mmio / clock sẽ nối tiếp vào file này)*

---

## 0. Bằng chứng

Artifact: `phases/phase02_embedded/`. Build xanh 6/6 preset (`ALL GREEN`).

Cách chứng minh: `src/elision_probe.cpp` **không bao giờ được link**. Build compile nó thành
**assembly ở `-O2`**, test đọc file `.s`. Không phải lời khẳng định — là output của chính compiler.

> ⚠️ Phải compile **riêng ở `-O2`**, bỏ qua flag của project. Debug thì không tối ưu gì, sanitizer thì
> chèn instrumentation vào mọi truy cập bộ nhớ — cả hai đều **phá huỷ đúng thứ đang cần đo**. Câu hỏi
> *"compiler có bỏ bớt lệnh đọc không?"* chỉ có câu trả lời ở `-O2`.

### Bằng chứng 1 — không `volatile` thì compiler bỏ đọc

```c
int read_plain_ten_times(void) { for (i<10) sum += g_plain; }
```
```asm
read_plain_ten_times:
    movl  g_plain(%rip), %eax    ; ĐỌC ĐÚNG 1 LẦN
    leal  (%rax,%rax,4), %eax    ; ×5
    addl  %eax, %eax             ; ×2  →  ×10
    ret                          ; KHÔNG CÓ VÒNG LẶP
```
10 lần đọc trong source → **1 lần đọc trong máy**. Vòng lặp biến mất hoàn toàn.

### Bằng chứng 2 — có `volatile` thì mọi lệnh đọc sống sót

```c
int read_volatile_ten_times(void) { for (i<10) sum += g_volatile; }
```
```asm
.L4:
    movl  g_volatile(%rip), %ecx   ; đọc thật
    addl  %ecx, %edx
    subl  $1, %eax
    jne   .L4                      ; VÒNG LẶP CÒN NGUYÊN → chạy đủ 10 lần
```
Khác đúng **một từ khoá**.

### ⭐ Bằng chứng 3 — `volatile` KHÔNG phải atomic

| | Assembly | Chạm bộ nhớ |
|---|---|---|
| `g_volatile = g_volatile + 1` | `movl g_volatile(%rip), %eax` / `addl $1, %eax` / `movl %eax, g_volatile(%rip)` | **2 lần** (đọc rồi ghi) |
| `g_atomic.fetch_add(1)` | `lock addl $1, g_atomic(%rip)` | **1 lần, không chia cắt được** |

Giữa lệnh đọc và lệnh ghi của `volatile` có một **khe hở**. Bất cứ chuyện gì cũng có thể xảy ra ở đó.

### Bằng chứng 4 — khe hở đó lớn cỡ nào (`DISABLED_IncrementLosesUpdates`)

```
4 thread × 200 000 lần tăng trên volatile int
kỳ vọng 800 000  →  thực tế 256 310  →  MẤT 543 690  (68 %)
```

Test này **là** một data race theo định nghĩa ⇒ để sau `DISABLED_` cho gate TSan xanh. Chạy tay:
```bash
./build/debug-17/phases/phase02_embedded/phase02_volatile_test --gtest_also_run_disabled_tests
```

---

## 1. ⭐ Bài học cốt lõi

> **`volatile` nói chuyện với COMPILER. `atomic` nói chuyện với CPU.**
> Hai vấn đề khác nhau. Không cái nào thay được cái nào.

| | Chặn ai | Giải quyết |
|---|---|---|
| `volatile` | **compiler** bỏ bớt / gộp / đổi thứ tự lệnh | giá trị **tự đổi** ngoài tầm kiểm soát của chương trình |
| `std::atomic` | **CPU + compiler** | nhiều thread cùng đụng một ô nhớ |

Dùng khi nào:

- **`volatile`** → thanh ghi phần cứng (MMIO), biến bị sửa trong **ISR** trên cùng 1 core,
  `sig_atomic_t` với signal handler. **Hết.**
- **`std::atomic`** → mọi thứ liên quan đến **thread**.
- Cần cả hai → `std::atomic<T>` với `volatile` qualifier, hoặc thiết kế lại. Rất hiếm khi cần.

⚠️ **C++20 deprecate `++`, `--`, `+=` trên `volatile`** (P1152R4) — vì chúng *trông như một thao tác*
mà thật ra là ba. Uỷ ban chuẩn xác nhận chính cái bẫy này.

---

## 2. Vì sao thanh ghi phần cứng BẮT BUỘC phải `volatile`

```c
while (*STATUS_REG == 0) { }   // chờ phần cứng báo xong
```

Không có `volatile`: compiler thấy trong vòng lặp **không có gì gán vào** `*STATUS_REG`, kết luận giá
trị không đổi ⇒ đọc 1 lần ⇒ **vòng lặp vô hạn**. Code đúng logic, chạy treo máy, và **chỉ treo ở bản
release** (debug không tối ưu nên vẫn chạy). Đây là kiểu bug kinh điển nhất của embedded.

Có `volatile`: mỗi vòng phải đọc thật ⇒ thấy phần cứng đổi giá trị ⇒ thoát.

---

## 3. `volatile` KHÔNG làm được gì (hay bị nhầm) ⭐

1. ❌ **Không atomic** — đã chứng minh, mất 68 % update.
2. ❌ **Không phải memory barrier** — nó chỉ giữ thứ tự **giữa các truy cập volatile với nhau**. Một
   lệnh ghi biến thường vẫn có thể bị CPU đẩy qua sau một lệnh ghi volatile.
3. ❌ **Không đảm bảo thread khác nhìn thấy** — không có cache coherence protocol nào được kích hoạt.
4. ❌ **Không chống được reorder của CPU** — nó là chỉ thị cho compiler, CPU không biết `volatile` là gì.
5. ⚠️ **Làm chậm** — cấm mọi tối ưu trên biến đó. Dùng thừa là mất hiệu năng vô ích.

---

## 4. Bẫy kỹ thuật đã gặp

### 4.1 ⭐ Đếm sai đơn vị đo

Lần đầu tôi assert *"phải thấy ≥ 10 lệnh load"* → **fail, chỉ thấy 1**. Vì compiler giữ **1 lệnh load
nằm trong vòng lặp chạy 10 lần**, không trải phẳng thành 10 lệnh.

> **Bài học:** *"bao nhiêu lệnh trong file"* ≠ *"bao nhiêu lần thực thi"*. Phép đo đúng là **vòng lặp
> còn hay mất** (có nhánh nhảy lùi hay không).

Đây chính xác là lý do W6 sẽ tách bạch **static analysis** (đếm lệnh) và **measurement** (đếm lần chạy)
khi tính WCET.

### 4.2 `extern "C"` cho probe

Không có nó, symbol trong `.s` là `_Z20read_plain_ten_timesv` — test phải tự giải mã Itanium mangling.
`extern "C"` giữ nguyên tên đã viết.

### 4.3 `ProbeAssemblyExists` phải tồn tại

Nếu probe build hỏng, file `.s` rỗng ⇒ mọi test dưới **pass im lặng** trên danh sách rỗng. Một test
riêng khẳng định file có nội dung. *(Cùng loại bẫy với `.clang-tidy` chạy trên danh sách file rỗng.)*

---

## 5. Câu hỏi phỏng vấn (→ `docs/interview/cpp_embedded.md`)

1. ⭐ **`volatile` là gì? Vì sao nó KHÔNG phải atomic?**
2. Không có `volatile`, `while (*reg == 0) {}` chạy ra sao? Vì sao chỉ hỏng ở bản release?
3. `volatile` có phải memory barrier không? Nó giữ thứ tự **giữa những gì**?
4. Khi nào dùng `volatile`, khi nào dùng `std::atomic`? Có khi nào cần cả hai?
5. Vì sao C++20 deprecate `++` trên `volatile`?
6. `volatile` có ảnh hưởng hiệu năng không?
7. Chứng minh `volatile` không atomic **bằng assembly** — bạn kỳ vọng thấy gì?

---

## 6. Nối vào capstone

- **W3 tiếp** — `mmio.hpp` dùng `volatile` đúng chỗ: register bank, read-to-clear, write-1-to-clear
- **W6** — timestamp; và bài học "đếm lệnh ≠ đếm lần chạy" quay lại ở WCET
- **W8/W9** — CAN driver đọc thanh ghi trạng thái: `volatile` cho register, `atomic` cho hàng đợi
  giữa ISR và thread
