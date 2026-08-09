# W45 — Process / `fork` · `exec` · `wait` · Scheduling · `/proc`

## 0. Bằng chứng

Artifact `phase07_linux/src/process_spawner.cpp` (`fork`+`execv`+`waitpid`, stdout qua `pipe`) + `read_vm_rss_kb()` parse `/proc/self/status`. Test xanh dưới `-20`/`-23`, asan, ubsan.

| Đo | Kết quả | Chốt |
|---|---|---|
| `run("/bin/echo",{"hello","world"},capture)` | `success()`, output = `"hello world\n"` | ⭐ stdout con đọc được ở cha → **fd kế thừa qua `execv`** |
| `run("/bin/false",{})` | `exited()`, `exit_code==1` | thoát bình thường mã ≠ 0 (không phải signal) |
| `run("/bin/sh",{"-c","kill -TERM $$"})` | `signaled()`, `term_signal==15` | ⭐ `WIFSIGNALED` — bị giết ≠ `WIFEXITED` |
| malloc 64 MiB → memset | `ΔVmRSS > 48 MiB` | ⭐ page chỉ resident **khi chạm** — demand paging, họ hàng COW |

⭐ **Chốt đo được:** một tiến trình con hoặc *thoát* (`exited()`, đọc `exit_code`) hoặc *bị giết* (`signaled()`, đọc `term_signal`) — **loại trừ nhau**. Gộp hai đường này là bug kinh điển. Và bộ nhớ ẩn danh **không tốn RAM tới khi bị chạm** — cùng cơ chế page-fault-on-first-touch khiến `fork()` COW rẻ.

---

## 1. `fork()` + Copy-on-Write ⭐

`fork()` tạo tiến trình con là **bản sao** của cha: page table riêng, nhưng ban đầu cả hai **trỏ chung physical page** (đánh dấu read-only). Khi một bên **ghi** → page fault → kernel cấp page mới, copy, chỉ khi đó mới tách.

- Hệ quả: `fork()` một tiến trình 8 GB **không** copy 8 GB — chỉ copy page table (vài KB). Nếu con `exec()` ngay → gần như không page nào bị nhân bản (exec thay sạch address space).
- ⚠️ **`fork()` trong đa luồng**: chỉ luồng gọi `fork` sống ở con, nhưng mutex/heap giữ nguyên trạng thái. Nếu luồng khác đang giữ lock `malloc` lúc fork → con `malloc()` = **deadlock**. Sau `fork` chỉ được gọi **async-signal-safe** functions tới `exec`. → đây là lý do `posix_spawn` tồn tại.

## 2. `exec` family — thay ruột, giữ vỏ ⭐

`execv(path, argv)` **thay nội dung** tiến trình (text/data/heap/stack mới) nhưng **giữ**: PID, parent, **file descriptor đang mở** (trừ fd có cờ `FD_CLOEXEC`), working dir, signal dispositions cơ bản.

- ⭐ **fd inheritance chính là cơ sở của shell redirect/pipe:** shell `fork`, ở con `dup2` fd cho `>`/`|`, rồi `exec` — chương trình mới thấy stdout đã bị nối sẵn. Artifact tuần này làm đúng vậy: `dup2(pipe_w, STDOUT_FILENO)` trong con trước `execv`.
- `execv` **không return** khi thành công (image cũ biến mất). Chỉ return khi **lỗi** → phải `_exit(127)` ngay, **không** `return`/`exit()`: tránh flush lại stdio buffer của cha (đã bị copy sang con), tránh chạy tiếp code cha trong con.
- `execv` nhận `char* const argv[]` (không `const`) — di sản POSIX trước khi C có `const`; dùng `std::string::data()` (char* từ C++17) né `const_cast`.

## 3. `wait` — thu hoạch & zombie

- `waitpid(pid,&status,0)` chặn tới khi con kết thúc, **reap** nó (giải phóng entry trong bảng tiến trình). Không wait → con thành **zombie** (đã chết nhưng entry còn, giữ PID).
- Giải mã `status` bằng macro, **không** so sánh số trực tiếp:
  - `WIFEXITED` → `WEXITSTATUS` (0–255).
  - `WIFSIGNALED` → `WTERMSIG` (vd 15=SIGTERM, 9=SIGKILL, 11=SIGSEGV).
- `waitpid` bị ngắt bởi signal → `EINTR`, phải loop lại.

## 4. Scheduling (tổng quan)

- Kernel cũ: **CFS** (Completely Fair Scheduler) — chia CPU theo *vruntime*, trọng số từ `nice` (−20…+19, thấp = ưu tiên cao). Kernel mới (6.6+): **EEVDF**.
- `nice`/`renice` đổi trọng số; `chrt` đặt real-time policy (`SCHED_FIFO`/`SCHED_RR`) cho tác vụ latency-critical. Xem `/proc/<pid>/sched`, `/proc/<pid>/stat`.
- Ngữ cảnh senior: real-time policy + CPU pinning (`taskset`, `sched_setaffinity`) là công cụ giảm jitter cho hot path — nhưng lạm dụng gây starvation.

## 5. `/proc` — cửa sổ kernel dạng text ⭐

Filesystem ảo: đọc file = kernel sinh nội dung on-the-fly.
- `/proc/self/status` → `VmSize` (**virtual** đã map, kể cả chưa chạm/reserved) vs `VmRSS` (**resident** trong RAM lúc này). Chênh lệch = lazy mapping, bình thường.
- `/proc/<pid>/maps` → từng vùng nhớ (heap, stack, .so, mmap). `/fd/` → fd đang mở (chứng minh inheritance). `/stat` → state, utime/stime, nice.
- Đây là cách `ps`/`top`/`htop` lấy số — chúng chỉ là parser của `/proc`.

---

## 6. TỰ VIẾT

<!-- Tự tay viết lại, không nhìn:
     - fork+COW: vì sao fork tiến trình 8GB không copy 8GB? Ghi lần đầu xảy ra gì?
     - fork trong đa luồng nguy hiểm chỗ nào? async-signal-safe nghĩa là gì?
     - exec giữ lại gì, mất gì? FD_CLOEXEC để làm gì? vì sao execv fail phải _exit không return?
     - zombie là gì, tránh bằng cách nào? phân biệt WIFEXITED vs WIFSIGNALED.
     - VmSize vs VmRSS khác nhau ra sao? /proc/self/status sinh nội dung thế nào? -->

---

## 7. Câu hỏi phỏng vấn nối tiếp

1. `fork()` một tiến trình 8 GB có copy 8 GB không? Ghi lần đầu sau fork tốn gì? (COW; page fault + alloc + memcpy 1 page)
2. Con `fork` rồi `exec` ngay — vì sao rất rẻ dù "copy cả tiến trình"? (exec thay address space → gần như không page COW nào bị nhân bản)
3. Vì sao `execv` fail phải `_exit(127)` chứ không `return`? (image cũ còn nguyên; return/exit sẽ flush stdio buffer cha nhân đôi + chạy tiếp code cha trong con)
4. fd nào sống sót qua `exec`, fd nào không? (mọi fd trừ `FD_CLOEXEC`; đây là nền của shell redirect/pipe)
5. Zombie process là gì, sinh ra sao, dọn thế nào? (con chết cha chưa `wait`; `waitpid` reap; hoặc `SIGCHLD`/double-fork)
6. `VmSize` 500 MB nhưng `VmRSS` 12 MB — máy sắp hết RAM? (không; virtual ≫ resident là bình thường, lazy/demand paging)
7. `fork()` trong chương trình đa luồng — cạm bẫy? (chỉ luồng gọi sống ở con; lock giữ bởi luồng khác kẹt vĩnh viễn → chỉ async-signal-safe tới exec; dùng `posix_spawn`)

**What would a Staff Engineer improve?**
- Dùng **`posix_spawn`** thay `fork`+`exec` tay khi có thể: nó tránh cả lớp lỗi fork-trong-đa-luồng và trên Linux dùng `clone(CLONE_VM|CLONE_VFORK)` rẻ hơn với tiến trình lớn.
- Đặt **`FD_CLOEXEC`** (hoặc `pipe2(O_CLOEXEC)`) mặc định cho mọi fd nội bộ để chúng **không** rò rỉ vào tiến trình con — fd leak là lỗ hổng bảo mật (con thừa hưởng socket/secret) và bug giữ tài nguyên.
- Bọc pid/fd bằng **RAII** (đóng fd trong destructor, `waitpid` khi hủy) để exception-safe: hiện `run()` nếu ném giữa chừng vẫn ổn, nhưng một `Process` object dài hạn cần RAII thật.
- Xử lý **timeout**: con treo → `run()` treo. Production cần `waitpid(WNOHANG)` + `kill(SIGTERM)`→`SIGKILL` leo thang, hoặc `pidfd_open` + `poll`.
- Đo tài nguyên con bằng **`wait4`/`getrusage`** (CPU time, max RSS con) thay vì chỉ exit code — cần cho sandbox/CI runner.
