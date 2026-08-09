# W46 — Virtual Memory · `mmap` · Page Faults · Huge Pages

## 0. Bằng chứng

Artifact `phase07_linux/src/mapped_file.cpp` — `MappedFile` RAII quanh `mmap`/`munmap` (move-only) + `read_page_faults()` đọc `ru_minflt`/`ru_majflt` từ `getrusage(RUSAGE_SELF)`. Test xanh dưới `-20`/`-23`, asan, ubsan.

| Đo | Kết quả | Chốt |
|---|---|---|
| map file rồi đọc bytes | khớp từng byte đã ghi | ⭐ `mmap` = view zero-copy, không `read()` copy |
| touch mọi trang file 32 MiB (8192 trang) | `ΔVmRSS = 32900 kB` (~= kích thước file) | ⭐ trang chỉ resident **khi chạm** — demand paging |
| — cùng lần đó | `Δminor = 16`, `Δmajor = 0` | ⭐ **2056 KiB/fault ≈ 2 MiB** → kernel map cả **large folio** mỗi fault |
| file rỗng | `empty()`, không gọi `mmap` | `mmap` length 0 = `EINVAL` → phải chặn |
| `MappedFile b = std::move(a)` | `a` rỗng, `b` giữ mapping, asan sạch | move cướp quyền → chỉ **một** `munmap` |

⭐ **Chốt đo được:** `mmap` **không cấp RAM** — nó tạo VMA, RAM chỉ vào khi *chạm* (RSS nhảy đúng lúc touch). Và **số page fault phụ thuộc kernel**: máy này map ~2 MiB mỗi fault (large folios) nên 8192 trang chỉ tốn 16 fault. Vì thế **tín hiệu ổn định là ΔVmRSS (bytes resident), không phải số fault** — số fault chỉ dùng để phân biệt minor (=16, cache nóng) vs major (=0, không I/O đĩa).

---

## 1. `mmap` = ánh xạ, KHÔNG phải cấp RAM ⭐

`read()` **sao chép** dữ liệu kernel → buffer. `mmap()` **ánh xạ** file thẳng vào không gian địa chỉ ảo: trả về con trỏ, truy cập file như một mảng — không copy.

- Lúc `mmap` trả về: kernel chỉ ghi một **VMA** (virtual memory area) vào bảng vùng nhớ của tiến trình — "dải địa chỉ này ↔ file kia". **Chưa byte nào trong RAM.**
- Chạm `p[i]` lần đầu → **page fault** → kernel nạp đúng trang chứa `p[i]` (từ page cache hoặc đĩa) rồi cho lệnh chạy tiếp. Đo được: `ΔVmRSS` nhảy đúng bằng lượng đã chạm.
- Vì thế `mmap` file 4 GB trên máy 2 GB RAM **thành công ngay** — 4 GB là *virtual* (`VmSize`), resident (`VmRSS`) chỉ tăng theo phần bạn chạm. Đây đúng ranh giới `VmSize` vs `VmRSS` của W45.

⭐ *Cấp phát địa chỉ* (mmap — rẻ, tức thì) ≠ *cấp phát bộ nhớ vật lý* (page fault — lười, khi chạm). Ranh giới quan trọng nhất của virtual memory.

## 2. Page fault: minor vs major ⭐

Page fault = CPU truy cập địa chỉ ảo mà page table **chưa có ánh xạ vật lý hợp lệ** → kernel nhảy vào xử lý.

| | **Minor** | **Major** |
|---|---|---|
| Trang đang ở đâu | **đã trong RAM** (page cache / COW) | **còn trên đĩa/swap** |
| Kernel làm | chỉ nối PTE | **đọc I/O** rồi mới nối |
| Chi phí | ~nano giây | ~micro→mili giây (chậm ngàn lần) |
| Đo | `ru_minflt` (`getrusage`) | `ru_majflt` |

Test tuần này: file vừa `write()` nên page cache **nóng** → mọi fault là **minor** (`Δmajor = 0`). Chương trình "chậm bí ẩn" thường do `major` đội lên (đang swap, hoặc đọc file lạnh). `/usr/bin/time -v` in cả hai số này.

⚠️ Page fault là **bình thường, phục hồi được** (mỗi lần cấp RAM lười đều là fault). **SIGSEGV** chỉ khi địa chỉ **không thuộc VMA hợp lệ nào** — fault không xử lý được. "Segmentation fault" = fault thất bại, không phải fault nói chung.

## 3. Huge pages / large folios ⭐

CPU dịch địa chỉ ảo→vật lý qua **TLB** — cache dịch rất nhỏ (vài trăm entry). Trang 4 KiB: 1 GB = 262.144 trang → TLB không đủ → TLB miss + page-table walk liên tục. Trang **2 MiB**: 1 GB = 512 trang → một entry phủ 2 MiB → TLB hit cao vọt, ít walk. Đó là lý do DB/JVM/hypervisor bật huge pages cho vùng nóng lớn.

- Lợi ích từ **dịch địa chỉ (TLB)**, KHÔNG phải RAM đọc/ghi nhanh hơn — băng thông RAM như nhau.
- ⭐ Đo được tuần này: 8192 trang 4 KiB chỉ tốn **16 fault** (2 MiB/fault). Kernel này (Linux 6.x+) dùng **large folios** — page cache gom nhiều trang liền kề thành một "folio" tới 2 MiB, map cả folio trong một fault. Cùng họ với **THP** (transparent huge pages, `/sys/kernel/mm/transparent_hugepage`) và **fault-around** (`filemap` map ~16 trang quanh chỗ fault). Cả ba đều làm **số fault ≪ số trang** → đừng bao giờ assert "một fault mỗi trang".

## 4. `mmap` cờ & công dụng

| Cờ | Ý nghĩa |
|---|---|
| `PROT_READ/WRITE/EXEC` | quyền trên trang (vi phạm → SIGSEGV) |
| `MAP_PRIVATE` | copy-on-write: ghi tạo bản riêng, không đụng file |
| `MAP_SHARED` | ghi phản chiếu ra file / các tiến trình khác thấy → nền của shared memory |
| `MAP_ANONYMOUS` | không gắn file, trang demand-zero — `malloc` lớn gọi cái này bên dưới |

fd **đóng được ngay sau `mmap`**: mapping giữ ref riêng tới file (artifact làm đúng vậy — `close(fd)` trước khi trả về).

## 5. RAII cho mapping

`mmap` cấp tài nguyên kernel → phải `munmap`. `MappedFile`:
- **move-only**: cướp `addr_/size_`, set nguồn về null → destructor chỉ `munmap` **một lần** (asan bắt double-munmap nếu sai — test `MoveTransfersOwnership` chứng minh sạch).
- factory `open_readonly` ném `std::system_error` khi `open/fstat/mmap` lỗi; `mmap` trả `MAP_FAILED` (**≠ nullptr**!) — phải so với `MAP_FAILED`.
- file rỗng → không `mmap` (tránh `EINVAL`), map rỗng hợp lệ.

---

## 6. TỰ VIẾT

<!-- Tự tay viết lại, không nhìn:
     - mmap file 4GB trên máy 2GB RAM: vì sao thành công ngay? VmSize vs VmRSS?
     - phân biệt cấp phát địa chỉ (VMA) vs cấp phát RAM (page fault). Chạm trang lần đầu xảy ra gì?
     - minor vs major fault khác gì? cái nào có I/O? SIGSEGV khác page fault thường ra sao?
     - huge page tăng tốc nhờ đâu (TLB, không phải tốc độ RAM)? large folios/THP/fault-around làm số fault thế nào?
     - vì sao MappedFile phải move-only? sai thì asan báo gì? mmap trả gì khi lỗi? -->

---

## 7. Câu hỏi phỏng vấn nối tiếp

1. `mmap` file 4 GB trên máy 2 GB RAM — lỗi "hết bộ nhớ"? (không; chỉ tạo VMA, RAM vào khi chạm; 4 GB là virtual)
2. Đọc file lớn: `read()` vs `mmap` — khi nào chọn cái nào? (mmap: truy cập ngẫu nhiên/zero-copy/chia sẻ; read: tuần tự, stream, pipe, cần kiểm soát I/O)
3. minor vs major page fault — cái nào giết hiệu năng, đo bằng gì? (major có I/O đĩa; `getrusage.ru_majflt`, `/usr/bin/time -v`, `perf stat`)
4. Huge page tăng tốc nhờ đâu? RAM có đọc nhanh hơn không? (nhờ TLB — ít entry cho cùng lượng RAM; RAM **không** nhanh hơn)
5. `VmSize` 500 MB, `VmRSS` 12 MB — sắp hết RAM? (không; virtual ≫ resident là bình thường)
6. `mmap` trả gì khi lỗi, khác con trỏ hợp lệ ra sao? (`MAP_FAILED` = `(void*)-1`, **không** phải `nullptr`)
7. `MAP_PRIVATE` vs `MAP_SHARED` khi ghi? (`PRIVATE` = COW bản riêng, không đụng file; `SHARED` = phản chiếu ra file/tiến trình khác)

**What would a Staff Engineer improve?**
- **`madvise`** theo pattern truy cập: `MADV_SEQUENTIAL` (tăng readahead) / `MADV_RANDOM` (tắt readahead) / `MADV_WILLNEED` (prefetch) / `MADV_DONTNEED` (nhả trang). Chọn đúng có thể đổi hẳn hồ sơ fault.
- **`MAP_POPULATE`** để prefault toàn mapping ngay lúc map (đổi latency lúc chạm lấy latency lúc mở) — hợp cho tiến trình muốn "warm" trước khi vào hot path.
- **Huge pages tường minh** cho arena lớn: `MADV_HUGEPAGE` hoặc `MAP_HUGETLB` — giảm áp lực TLB cho vùng nóng, nhưng tốn RAM theo bội 2 MiB và có thể gây phân mảnh.
- **Kích thước file thay đổi**: `mmap` chụp `st_size` một lần; file bị truncate sau đó → chạm vùng quá EOF = **SIGBUS** (khác SIGSEGV). Production cần bắt SIGBUS hoặc dùng `MAP_SHARED_VALIDATE`/khoá file.
- **Đo đúng**: RSS/`ru_minflt`/`ru_majflt` là chỉ số/tiến trình, dao động theo tải máy — benchmark cần cô lập (cgroup, `MAP_POPULATE`, drop_caches cho major fault lạnh) và lặp nhiều lần.
