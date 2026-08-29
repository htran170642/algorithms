# Tuần 3 — Linux IPC: epoll, Unix socket, shared memory

> Bản tiếng Việt của [w03_ipc.md](w03_ipc.md). Thuật ngữ kỹ thuật giữ nguyên
> tiếng Anh, vì đó là từ bạn sẽ gặp trong man page, trong code, và trong phòng
> phỏng vấn.

## 1. Khái niệm

Tuần 2 chuyển dữ liệu giữa các **thread** (một address space, một cái mutex là
đủ). Tuần 3 chuyển nó giữa các **process**, và điều đó thay đổi những gì làm
được:

| | thread | process |
|---|---|---|
| Chia sẻ con trỏ | được | **không** — một địa chỉ chẳng có nghĩa gì với bên kia |
| Đồng bộ bằng `std::mutex` | được | chỉ khi chính cái mutex nằm trong shared memory |
| Một bên crash | kéo bên kia chết theo | bên kia vẫn sống |
| Giá của một lần bàn giao | một cache line | một syscall, hoặc một trang đã map |

Dòng thứ ba là lý do một cockpit dùng process. Nếu IVI renderer segfault,
cluster vẫn phải hiện tốc độ. Thread không cho bạn điều đó; address space riêng
thì có. Đây chính là lập luận mà hypervisor đưa ra ở một tầng cao hơn trong
tuần 13 — freedom from interference, ở một mức chi tiết khác.

Ba cơ chế, ba việc khác nhau:

- **`epoll`** — một thread, nhiều descriptor. Đăng ký quan tâm một lần, chặn cho
  tới khi *có gì đó* sẵn sàng. Thay thế "mỗi fd một thread" (tốn kém, và giờ mọi
  thứ cần lock) và "poll trong vòng lặp" (đốt một core chỉ để biết chưa có gì).
- **Unix domain socket** — kernel làm việc copy và đồng bộ hộ. Tốn hai lần copy
  và hai syscall mỗi message. Đổi lại bạn có ngữ nghĩa kết nối, phát hiện peer
  chết, và không có state chung nào để làm hỏng.
- **Shared memory** — cả hai process map cùng các trang vật lý. Một lần copy,
  không syscall. Đổi lại bạn tự viết phần đồng bộ, bằng atomic, và một cú crash
  giữa lúc cập nhật để bên kia nhìn vào dữ liệu ghi dở.

## 2. Vì sao nó tồn tại trong Cockpit DC

Kiến trúc trong CLAUDE.md có cluster (liên quan an toàn, thường là QNX) và IVI
(Android, khổng lồ, code bên thứ ba) là hai domain tách rời. Giữa chúng, vehicle
signal vẫn phải chảy qua. Chọn transport nào là **một lập luận về an toàn,
không phải về hiệu năng**:

- **Socket** là mặc định an toàn. Không chia sẻ gì nghĩa là không có gì làm hỏng
  được, và `EPIPE` / `EPOLLRDHUP` báo cho bạn biết peer đã chết. Dùng nó cho
  control, configuration, request/response, mọi thứ tần suất thấp.
- **Shared memory** dành cho hot path — một video frame, một point cloud, một
  bộ signal 100 Hz — nơi 10x latency và 10x CPU thực sự có ý nghĩa. Mỗi lần
  dùng đều phải trả lời được: "reader thấy gì nếu writer chết giữa lúc ghi?"

Phép đo bên dưới là thứ biến điều đó thành một quyết định thay vì một sở thích.

## 3. Kiến trúc

```text
             cluster process                    IVI process
        +----------------------+          +---------------------+
        |   epoll (một thread) |          |                     |
        |     |            |   |          |                     |
        |  ctrl fd      data   |          |                     |
        +-----|------------|---+          +---------------------+
              |            |                         |
              |            +----- shared memory -----+   hot path
              |                    (SharedRing)          1 copy, 0 syscall
              |
              +------------------ unix socket ---------+   control path
                                  (SOCK_SEQPACKET)         2 copy, 2 syscall
```

Cả hai hướng gặp lại nhau ở tuần 8, nơi middleware chọn **theo từng signal**
chứ không phải theo từng process.

## 4. Cài đặt C++ / Linux

| File | Vai trò |
|---|---|
| [libs/av_ipc/include/av/ipc/unique_fd.hpp](../libs/av_ipc/include/av/ipc/unique_fd.hpp) | RAII cho một descriptor — Rule of 5 viết đúng một lần |
| [libs/av_ipc/include/av/ipc/unix_socket.hpp](../libs/av_ipc/include/av/ipc/unix_socket.hpp) · [.cpp](../libs/av_ipc/src/unix_socket.cpp) | endpoint `SOCK_SEQPACKET`, `IoStatus`, chế độ non-blocking |
| [libs/av_ipc/include/av/ipc/poller.hpp](../libs/av_ipc/include/av/ipc/poller.hpp) · [.cpp](../libs/av_ipc/src/poller.cpp) | wrapper epoll, level-triggered, `EPOLLRDHUP` |
| [libs/av_ipc/include/av/ipc/shared_ring.hpp](../libs/av_ipc/include/av/ipc/shared_ring.hpp) · [.cpp](../libs/av_ipc/src/shared_ring.cpp) | `shm_open` + `mmap`, ring SPSC lock-free |
| [apps/ipc_bench/main.cpp](../apps/ipc_bench/main.cpp) | phép đo, qua một `fork()` thật |

### Quyết định đáng bảo vệ

**`SOCK_SEQPACKET`, không phải `SOCK_STREAM`.** SEQPACKET là loại AF_UNIX duy
nhất vừa tin cậy, vừa giữ thứ tự, *và* giữ ranh giới message. Một `CanFrame`
gửi đi như một message thì tới nơi như một message. Với STREAM bạn phải tự chế
ra length prefix và ghép lại các lần đọc dở — nguồn bug lớn nhất khi người ta
chọn STREAM theo quán tính. Test `PreservesMessageBoundaries` là bằng chứng.

**`MSG_NOSIGNAL` ở mọi lần send.** Không có nó, ghi vào một socket mà peer đã
biến mất sẽ sinh `SIGPIPE`, mà hành vi mặc định là **giết process**. Một cockpit
phải biết peer chết qua một mã trả về.

**`IoStatus::WouldBlock` không phải lỗi.** Trên một fd non-blocking đó là câu
trả lời bình thường, nghĩa là "quay lại khi epoll bảo". Gộp nó vào `Error` là
cách một vòng poll biến thành busy-wait hoặc một cú shutdown vô cớ.

**epoll level-triggered, không phải edge-triggered.** Edge-triggered nhanh hơn
nhưng buộc bạn phải đọc cạn mọi fd tới `EAGAIN` ở mỗi lần thức; sót một lần là
fd đó im lặng mãi mãi. Level-triggered tha thứ cho một lần đọc dở. Tối ưu khi
profile bảo thế, không phải trước đó.

**`EPOLLRDHUP` luôn luôn.** Không có nó, một peer chết chỉ được phát hiện khi
tình cờ có ai đó đi đọc và nhận về 0 byte. Có nó, cú ngắt kết nối thành một
event.

**Listener tự unlink file socket của mình.** Đóng một socket không xóa path của
nó. Chỉ process đã tạo ra nó mới được unlink — một client mà unlink là đang xóa
một endpoint đang sống. Cùng quy tắc đó cho `shm_unlink`.

**Bộ đếm của ring là `uint64_t` đơn điệu tăng, không phải chỉ số.**
`tail - head` chính là số phần tử, không cần cờ "đầy hay rỗng?" riêng. Ở 1 MHz
sẽ mất 580.000 năm để tràn.

**`head` và `tail` nằm trên hai cache line khác nhau.** Producer và consumer mỗi
bên ghi một cái. Nếu chung một line, mỗi lần push sẽ vô hiệu hóa bản copy của
consumer và mỗi lần pop vô hiệu hóa bản của producer — false sharing, cách kinh
điển khiến một cấu trúc lock-free cuối cùng lại chậm hơn dùng mutex.

### Memory ordering, đầy đủ

Đây là phần đáng để dựng lại được từ số 0:

```cpp
// producer
tail = tail_.load(relaxed);      // chỉ thread này ghi tail
head = head_.load(acquire);      // ghép cặp với release của consumer
if (tail - head >= capacity) return false;
memcpy(slot(tail % capacity), &value, sizeof(T));
tail_.store(tail + 1, release);  // công bố thao tác ghi vào slot
```

- `relaxed` trên bộ đếm của chính mình: một thread không thể không thấy store
  của chính nó.
- `acquire` trên bộ đếm của bên kia: mọi thứ bên kia làm trước release store của
  nó đều hiện ra với ta sau đó.
- `release` khi công bố: `memcpy` **không được phép** bị sắp xếp lại xuống sau
  nó, nếu không consumer có thể đọc một slot chưa bao giờ được ghi.

Bỏ `release` đi và code vẫn pass mọi test trên x86, vì x86 không đảo thứ tự
store. Nó **hỏng trên ARM**, mà ARM chính là SoC thật của một cockpit. Đó là lý
do phải suy luận về chuyện này chứ không phải test nó.

## 5. Bài thực hành

```bash
./check.sh                             # 7 test binary, cả 4 sanitizer
./build/debug/apps/ipc_bench/ipc_bench
```

Đo trên máy này (round-trip, hai process, payload 32 byte, 20.000 mẫu sau 1.000
lần warm-up):

```text
  transport                    median          p99          max
  unix socket (RTT)          18.72 us     27.66 us    646.76 us
  shared memory (RTT)         1.89 us      2.38 us     29.61 us
```

**~10x ở median, ~12x ở p99.** Nhưng hãy đọc cả cột `max`: trường hợp tệ nhất
của socket là 646 us, đó là scheduler cho process ngủ rồi đánh thức muộn. Chính
cái đuôi đó — không phải median — mới là thứ làm một signal trượt frame.

Con số **không** có trong bảng: process con phía shared memory **quay vòng liên
tục**. Nó đốt trọn một core để lấy được 1.89 us đó. Một thiết kế thật sẽ ghép
ring với một `eventfd` để consumer ngủ được khi ring rỗng, đánh đổi vài us để
lấy lại CPU.

Thử nghiệm:

1. Tăng payload lên 4 KB. Khoảng cách của socket thu hẹp — chi phí copy bắt đầu
   lấn át syscall. Lựa chọn phụ thuộc vào **kích thước message**, không chỉ tần
   suất.
2. Xóa `release` khỏi `try_push` rồi chạy lại test. Mọi thứ vẫn pass trên x86.
   Đó chính là bài học.
3. Chạy `ipc_bench` dưới `taskset -c 0` để hai process dùng chung một core.
   Ring spin-wait sụp đổ; socket gần như không hề hấn, vì việc chặn cho phép
   peer được chạy.

## 6. Bài tập lỗi / debug

| Lỗi tiêm vào | Phát hiện bởi | Log ra | Fallback | Safe state |
|---|---|---|---|---|
| Process peer thoát | `EPOLLRDHUP` / `recv` trả `PeerClosed` | `ERROR ipc.socket` | bỏ kết nối, tiếp tục phục vụ những cái khác | vòng reconnect (tuần 8) |
| File socket cũ còn sót sau crash | `bind` sẽ báo `EADDRINUSE` | `WARN could not remove stale endpoint` | unlink rồi bind lại | khởi động lại luôn thành công |
| Path dài hơn `sun_path` (108) | kiểm tra độ dài trước khi copy | `ERROR path rejected` | từ chối listen | không bao giờ bind nhầm path |
| Hai bên cùng tạo một tên shm | `O_EXCL` trên `shm_open` | `ERROR shm_open create failed` | bên thứ hai bị từ chối | chỉ số không thể bị làm hỏng |
| Peer hiểu `sizeof(T)` khác | kiểm tra `element_size` trong `open()` | — | từ chối attach | không bao giờ đọc rác từ slot |
| `mmap` trước `ftruncate` | **không gì cả** — `SIGBUS` khi chạm lần đầu | không gì | không có | lý do `ftruncate` phải đi trước |
| Thiếu `release` store | **không gì cả trên x86** | không gì | không có | chỉ hỏng trên ARM, ngoài thực địa |

Hai dòng cuối là đóng góp của tuần 3 vào mẫu hình chung. Tuần 1 có một lỗi sinh
ra một con số sai nhưng hợp lý; tuần 2 có một lỗi không sinh ra output nào. Ở
đây ta có một lỗi sinh ra **hành vi đúng trên máy phát triển và sai trên
target**. Không lượng test nào trên x86 tìm ra nó — chỉ có đọc code với memory
model trong đầu.

## 7. Câu hỏi phỏng vấn cấp Senior

**1. Vì sao `SOCK_SEQPACKET` mà không phải `SOCK_STREAM`?**

Ranh giới message. STREAM là một ống byte: ba lần send 3, 2 và 4 byte có thể tới
nơi thành một lần đọc 9 byte, nên bên nhận cần framing riêng — một length
prefix, và một buffer ghép cho các lần đọc dở. SEQPACKET giữ nguyên ranh giới
mà vẫn tin cậy và đúng thứ tự. DGRAM cũng giữ ranh giới, nhưng không có kết nối,
nên bạn mất khả năng phát hiện peer chết.

**2. Khi nào bạn *không* dùng shared memory, dù nó nhanh gấp 10 lần?**

Khi payload nhỏ và thưa, vì tiết kiệm 17 us trên một signal 10 Hz là nhiễu. Khi
một trong hai bên không đáng tin hoặc hay crash, vì một writer chết giữa lúc cập
nhật để lại reader nhìn vào một cấu trúc rách nát và không có kernel nào dọn hộ.
Khi hai bên có vòng đời khác nhau, vì attach vào một vùng mà bên tạo đã biến mất
tự nó đã là cả một protocol. **Socket hỏng một cách sạch sẽ; shared memory hỏng
một cách im lặng.**

**3. epoll level-triggered hay edge-triggered?**

Level-triggered, trừ khi đo được là cần khác. Edge-triggered báo một *chuyển
tiếp*, nên bạn phải đọc cạn từng fd tới `EAGAIN` ở mỗi lần thức — sót một byte
là fd đó không bao giờ báo nữa, và bug trông giống như "một client ngẫu nhiên bị
treo". Level-triggered báo *trạng thái*, nên một lần đọc dở đơn giản là được báo
lại.

**4. `EAGAIN` trên một lần đọc non-blocking nghĩa là gì, và code phải làm gì?**

Ngay lúc này chưa có gì. Đó là trạng thái ổn định bình thường của một socket
rảnh, không phải lỗi. Code phải quay lại `epoll_wait`, không thử lại ngay (đó là
busy-wait) và không đóng kết nối (đó là shutdown vô cớ). `EINTR` là cái thứ hai
người ta hay làm sai: một signal tới trong lúc đang bị chặn, và phản ứng đúng là
**gọi lại**.

**5. Giải thích cặp acquire/release trong ring.**

`release` store của producer lên `tail` công bố mọi thứ nó đã ghi trước đó, bao
gồm cả cú copy vào slot. `acquire` load của consumer trên `tail` làm những thao
tác ghi đó hiện ra với nó. Không có cặp này, compiler hoặc CPU có thể đẩy thao
tác ghi slot xuống sau lần cập nhật bộ đếm, và consumer đọc một slot mà producer
chưa điền. Trên x86 thứ tự store che mất bug; trên ARM thì không.

**6. Vì sao `head` và `tail` phải nằm trên hai cache line khác nhau?**

Vì producer ghi cái này còn consumer ghi cái kia. Trên cùng một line 64 byte,
mỗi thao tác ghi vô hiệu hóa bản copy của core kia, nên mọi thao tác đều thành
một lần chuyển cache line giữa hai core — false sharing. Cấu trúc thì lock-free
mà vẫn chậm hơn mutex.

**7. Một process crash và giờ service không khởi động được: `bind` báo
`EADDRINUSE`. Vì sao, và sửa thế nào?**

File socket AF_UNIX sống lâu hơn process đã tạo ra nó; đóng fd không unlink
path. Cách sửa là `unlink` path cũ trước khi `bind`, coi `ENOENT` là trường hợp
bình thường. Điều tương tự áp dụng cho `shm_unlink` với POSIX shared memory —
nếu không `/dev/shm` tích tụ các vùng chết cho tới khi tmpfs đầy.

## 8. Checklist tự kiểm

- [x] Giải thích được mà không cần nhìn note
- [x] Tự cài đặt, không phải chỉ đọc
- [x] Cố ý làm hỏng và xem nó hỏng — file socket cũ còn sót, hai bên cùng tạo
      shm, sai `sizeof(T)`, message SEQPACKET bị cắt cụt
- [x] Bảo vệ được đánh đổi thiết kế đã chọn — SEQPACKET thay vì STREAM,
      level-triggered thay vì edge-triggered, socket cho control và shm cho hot
      path
- [x] Test pass dưới `./check.sh` — debug / asan / ubsan / **tsan** / tidy sạch

### Cố ý để lại cho sau

- **`eventfd` đi kèm ring** -> tuần 8, để consumer ngủ được thay vì quay vòng.
  Đây là cách sửa trung thực cho chi phí CPU đã đo ở trên.
- **Chống torn-read** (một sequence lock, hoặc generation counter mỗi slot) ->
  tuần 15, cùng phần còn lại của fault injection. Hiện tại một writer chết giữa
  lúc copy để lại một slot ghi dở và không gì phát hiện được.
- **Đọc source `can-utils`** (`candump.c`, `cansend.c`) -> tuần 5, nơi cùng vòng
  `epoll` đó nhận một socket `PF_CAN` thật thay vì một cái mô phỏng.
- **CPU affinity và scheduling priority** -> tuần 12, cùng mô hình thread của Qt,
  nơi priority inversion mới thực sự chạm tới được.
