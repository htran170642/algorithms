# Memory · Lifetime · Allocation · Exception Safety

Nguồn đáp án đầy đủ: [`notes/w01_no_heap.md`](../../notes/w01_no_heap.md)

---

## Q1. Tại sao embedded/automotive tránh dynamic allocation? ⭐

**Ý chính phải nói** — dẫn bằng *predictability + provability*, rồi kể ≥3 lý do:

1. **WCET không bị chặn** — duyệt free list · lấy lock · rơi vào `mmap` = syscall
2. **Fragmentation** — ECU chạy 15 năm không reboot; external fragmentation làm `malloc` fail dù
   tổng bộ nhớ trống còn nhiều
3. **Failure mode không có câu trả lời tốt** — `bad_alloc` cần exception (AUTOSAR hạn chế);
   `abort()` **không phải safe state**
4. **Không analyzable** — ISO 26262 đòi chặn trên chứng minh được
5. ⭐ **Freedom from interference** — QM + ASIL-D dùng chung heap ⇒ không còn độc lập

> ❌ **Bẫy:** trả lời *"vì nó chậm"*. `malloc` trung bình rất nhanh. Vấn đề là **phương sai** và
> **khả năng chứng minh**, không phải tốc độ.

**Đào sâu:** *"Không dùng heap thì cấp phát kiểu gì?"* → static/global · stack · **memory pool cấp
phát một lần lúc khởi động** (WCET hằng số vì free list cố định) · fixed-capacity container.

---

## Q2. "Freedom from interference" là gì? ⭐

**Ý chính:** thuật ngữ ISO 26262 — phải **chứng minh được** rằng component ASIL thấp (hoặc QM) không
thể làm hỏng component ASIL cao. Ba trục can thiệp: **memory** · **timing/execution** · **exchange of
information**.

Heap chung phá trục memory: một component leak làm component kia hết bộ nhớ.

**Đào sâu:** cơ chế đảm bảo → MPU/MMU partitioning · static allocation riêng · time partitioning
(budget CPU) · watchdog. Sẽ gặp lại ở W12/W13 (hypervisor) và W28.

---

## Q3. Tại sao move của `std::array` / container storage-inline không thể O(1)? ⭐

**Ý chính:** move = **chuyển giao quyền sở hữu**; muốn chuyển giao trong O(1) thì phải có
**indirection** để chuyển — tức một con trỏ.

- `std::vector`: data ở heap ⇒ tráo 3 con trỏ ⇒ O(1)
- `std::array` / `FixedVector`: data **trong** object ⇒ không có gì để ăn cắp ⇒ O(n)

**Đào sâu — tổng quát hoá:** `std::string` với **SSO** rơi vào *cả hai* vế: O(n) khi chuỗi ngắn
(inline), O(1) khi chuỗi dài (heap). Cùng một type.

> ⚠️ **Nuance đáng nói:** O(n) ≠ chậm. Move `std::array<int,8>` là memcpy 32 byte, có thể *nhanh hơn*
> move `std::vector`. **Big-O và chi phí thực tế là hai chuyện.**

**Hệ quả thực dụng:** đừng truyền by value; đừng để trong `std::vector` bị resize nhiều.

---

## Q4. Constructor ném exception giữa chừng — chuyện gì xảy ra? ⭐

**Ý chính — luật ngôn ngữ:** C++ chỉ hủy những gì đã construct **xong**.

- member + base class đã xong → **CÓ** được hủy, theo thứ tự ngược
- **destructor của chính object đó → KHÔNG BAO GIỜ chạy**
- ⇒ tài nguyên thô mà constructor giành được trong **thân hàm** sẽ **rò rỉ** nếu không tự dọn

**Đào sâu 1 — vì sao RAII tồn tại:** class có member `unique_ptr` thì an toàn tự động (member đã
construct xong *được* hủy). Class tự `new` hai lần trong thân constructor thì leak nếu lần hai ném.
Đây là **lý do sâu xa** của Rule of Zero, không phải chuyện code gọn.

**Đào sâu 2 — chi tiết ít người biết:** với `new T(...)` mà ctor ném, **bộ nhớ thô vẫn được giải
phóng** (runtime gọi `operator delete` tương ứng). Cái *không* chạy là **destructor**.

**Đào sâu 3:** *function-try-block* là cách duy nhất bắt exception ném từ **member initializer
list** — nhưng bắt buộc phải rethrow.

---

## Q5. `operator[]` và `at()` khác nhau ở **contract** nào?

**Ý chính:** không phải "một cái check một cái không" — mà là **ai chịu trách nhiệm** kiểm tra bound.

| | Contract | Dùng ở đâu |
|---|---|---|
| `operator[]` | **Precondition** — *caller* đảm bảo `i < size`. Vi phạm = UB. `assert` chỉ là lưới an toàn ở debug | vòng lặp nóng đã chứng minh được bound |
| `at()` | **Check** — *callee* kiểm tra, luôn luôn, cả ở release. Vi phạm = exception xác định | **trust boundary**: dữ liệu từ CAN, IPC, file |

**Đào sâu:** vì sao không phải lúc nào cũng dùng `at()`? Vì check trong vòng lặp nóng làm hỏng
vectorization và thêm nhánh — mà nhánh đó *không bao giờ* được lấy nếu bound đã chứng minh. Trả tiền
cho thứ mình không dùng là điều C++ cố tránh.

---

## Q6. `std::launder` giải quyết vấn đề gì?

**Ý chính:** khi bạn placement-new một object mới lên vùng storage cũ, compiler được phép **giữ giả
định cũ** về nội dung ở địa chỉ đó (nhất là với member `const` hoặc reference). `std::launder` nói
*"quên giả định cũ đi, ở đây giờ có object khác"*.

> Nhớ ngắn gọn: **đụng raw storage → cần `launder`**. Chi tiết tra lại khi cần.

**Bẫy:** `launder` **không** tạo object, không thay đổi lifetime. Nó chỉ chặn optimization dựa trên
giả định cũ.

---

## Q7. Tại sao dùng raw `std::byte` storage thay vì `T data_[N]`?

**Ý chính — hai lý do:**
1. `T data_[N]` **default-construct cả N phần tử ngay** ⇒ bắt `T` phải default-constructible
2. và làm việc không ai yêu cầu (dựng 64 object lúc boot)

Raw bytes + placement new ⇒ phần tử chỉ tồn tại khi thực sự được thêm vào.

---

## Q8. Khi nào `std::vector` **tốt hơn** fixed-capacity container?

**Ý chính:**
- capacity **không biết trước** / không chặn được theo thiết kế
- cần **một type duy nhất** cho mọi kích thước (fixed capacity nằm trong type ⇒ `FixedVector<int,8>`
  và `<int,16>` là hai type khác nhau)
- object bị **move/copy nhiều** (vector O(1), fixed O(n))
- kích thước biến động lớn ⇒ fixed capacity sẽ phí RAM theo trường hợp xấu nhất

> ❌ **Bẫy:** trả lời *"không bao giờ, embedded phải dùng fixed"*. Sai — không phải mọi code trong xe
> đều là ASIL. Phần infotainment/HMI dùng heap bình thường.

---

## Q9. `[[nodiscard]]` để làm gì trong API automotive?

**Ý chính:** biến việc **bỏ qua lỗi** từ *bug im lặng lúc 2h sáng* thành **lỗi compile**.

Pattern: `try_` prefix + trả `bool`/`optional` + `[[nodiscard]]` ⇒ lỗi là **giá trị** phải xử lý, và
không thể lờ đi do sơ ý. Đây là đường thay thế exception khi AUTOSAR hạn chế exception.

---

## Q10. WCET là gì, và đo max nhiều lần có ra WCET không? ⭐

**Ý chính:** WCET = **cận trên** của thời gian thực thi, dùng cho schedulability analysis
(`Σ Cᵢ/Tᵢ ≤ 1`).

> ⭐ **Câu chốt:** *"Giá trị đo được lớn nhất là cận DƯỚI của WCET, không phải WCET."*
> Measurement cho cận dưới (unsound), static analysis cho cận trên (sound nhưng bi quan).

**Đào sâu — vì sao WCET khó:** mọi thứ làm CPU nhanh *trung bình* đều làm WCET khó đoán — cache
(L1 vs DRAM chênh ~100×) · branch prediction · out-of-order/speculation · TLB · DRAM refresh ·
interrupt · DVFS · ⭐ **multicore interference** (core khác giành bus/cache chung).

⇒ Đó là lý do MCU an toàn dùng core in-order, cache khoá được hoặc scratchpad — **hy sinh throughput
để lấy predictability**.
