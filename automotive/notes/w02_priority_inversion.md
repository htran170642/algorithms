# W2 — Priority Inversion · Priority Inheritance

**Credit:** `std::thread`, mutex, `condition_variable`, atomic, `memory_order`, ThreadPool,
BlockingQueue, SpscRing — đã chứng minh ở `cplus_review` W32–W38. Tuần này chỉ làm **delta
automotive**: vì sao một mutex trong đường real-time là hiểm hoạ latency.

---

## 0. Bằng chứng

Artifact: `phases/phase01_rt_concurrency/`. Build xanh 6/6 preset.

⚠️ **Số đo còn TRỐNG** — máy dev có `ulimit -r = 0` nên `SCHED_FIFO` bị từ chối, 3 test `SKIPPED`
(message có kèm hướng dẫn cấp quyền). Chạy:

```bash
sudo ./build/debug-17/phases/phase01_rt_concurrency/phase01_priority_inversion_test
```

rồi điền vào đây:

| Cấu hình | HIGH bị chặn | Kỳ vọng |
|---|---|---|
| `PTHREAD_PRIO_NONE` (mặc định) | *(chưa đo)* | ≈ 250 ms = critical section 50 + MED 200 |
| `PTHREAD_PRIO_INHERIT` | *(chưa đo)* | ≈ 50 ms = chỉ critical section |
| Cải thiện | *(chưa đo)* | ≈ 5× |

⭐ **Chốt sẽ đo được:** HIGH và MED **không chia sẻ bất cứ thứ gì**, nhưng nếu không có priority
inheritance thì thời gian chặn của HIGH lại **bị chi phối bởi thời gian chạy của MED**.

---

## 1. Mars Pathfinder, 1997 — case study có thật ⭐

Tàu đáp xuống Sao Hoả, chạy vài ngày rồi **tự reset liên tục**. VxWorks, 3 task chia sẻ một bus:

| Task | Priority | Việc |
|---|---|---|
| `bc_dispatch` (bus manager) | CAO | quản lý bus, có watchdog |
| `ASI/MET` (khí tượng) | THẤP | ghi dữ liệu, **có lock bus** |
| Communications | TRUNG BÌNH | gửi dữ liệu về Trái Đất, **không đụng bus** |

1. `ASI/MET` (thấp) lấy mutex
2. `bc_dispatch` (cao) muốn bus → bị chặn
3. Communications (trung bình) chạy → **giành CPU của `ASI/MET`**
4. `ASI/MET` không chạy ⇒ không nhả mutex ⇒ `bc_dispatch` chờ mãi
5. **Watchdog hết giờ → reset**

JPL debug con tàu **đang ở trên Sao Hoả**, phát hiện cờ priority inheritance của VxWorks **đang TẮT**,
upload patch bật lên từ Trái Đất.

> ⭐ Câu để kể trong phỏng vấn: *"Task trung bình không hề dùng tài nguyên đó, nhưng nó là thủ phạm."*

---

## 2. Bounded vs Unbounded ⭐ — từ khoá là **unbounded**

**Bounded inversion — chấp nhận được:**
```
CAO chờ THẤP nhả lock
⇒ chờ ≤ độ dài critical section
⇒ tính được, đưa vào WCET được
```
Không tránh khỏi khi có chia sẻ tài nguyên. Không sao.

**Unbounded inversion — thảm hoạ:**
```
CAO chờ THẤP, nhưng TRUNG BÌNH giành CPU của THẤP
⇒ chờ ≤ (critical section) + (thời gian chạy của TRUNG BÌNH)
⇒ mà TRUNG BÌNH chạy bao lâu thì KHÔNG BIẾT
⇒ không có cận trên ⇒ WCET không tính được ⇒ hết real-time
```

Nối với W1: đây là **cùng một bệnh với `malloc`** — không phải "chậm", mà là **không có cận trên
chứng minh được**.

---

## 3. Ba cách chữa

| Cách | Cơ chế | Nhược điểm |
|---|---|---|
| **Priority Inheritance (PIP)** | THẤP **tạm mượn** priority của CAO khi đang giữ lock ⇒ TRUNG BÌNH không giành được CPU | Không chống deadlock; chained blocking phức tạp; tốn overhead runtime |
| **Priority Ceiling (PCP)** | Mutex có **trần** = priority cao nhất trong các task dùng nó; ai lock cũng lập tức chạy ở mức trần | Phải biết trước **mọi** user lúc thiết kế; nâng priority thừa. Bù lại: **chống được deadlock** |
| ⭐ **Không chia sẻ** | Lock-free SPSC queue · message passing · dữ liệu riêng từng thread | Phải thiết kế lại |

POSIX:
```cpp
pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);   // PIP
pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_PROTECT);   // PCP + setprioceiling
```

⚠️ Mặc định là **`PTHREAD_PRIO_NONE`** — priority inheritance **TẮT** trừ khi bạn bật. Đúng cái bẫy
Pathfinder dính.

> ⭐ **Cách thứ 3 mới là câu trả lời thật cho CDC.** Bạn đã có `SpscRing` ở `cplus_review` W38 — giờ
> mới rõ *vì sao* nó tồn tại: không có lock thì không có inversion.

---

## 4. Bẫy kỹ thuật đã gặp

### 4.1 ⭐ `PTHREAD_EXPLICIT_SCHED` — thiếu là hỏng im lặng

```cpp
pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);   // BẮT BUỘC
pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
pthread_attr_setschedparam(&attr, &param);
```

Mặc định là **`PTHREAD_INHERIT_SCHED`** — khi đó policy và priority set ở trên **BỊ BỎ QUA HOÀN
TOÀN**, thread kế thừa từ thread tạo ra nó. Không lỗi, không warning. Đây là cách kinh điển để "set"
một realtime priority mà **không bao giờ có hiệu lực**.

### 4.2 Vì sao dùng pthread thô, không dùng `std::thread`

Policy phải được set **trong attribute lúc tạo thread**. Tạo `std::thread` rồi sửa priority sau thì
có một **khoảng cửa sổ** thread đã chạy ở policy mặc định — race làm hỏng thí nghiệm.

### 4.3 Phải ghim 1 CPU

Nhiều core thì 3 thread chạy song song, **không có preemption ⇒ không có inversion để xem**.
`pthread_attr_setaffinity_np` ghim cả 3 vào CPU 0.

### 4.4 `busy_for` phải quay CPU thật, không được `sleep`

`sleep` nhường CPU — đúng cái ta cần MED **không** làm. Chỉ thread thực sự bận mới preempt được.

### 4.5 `CLOCK_MONOTONIC`, không phải `CLOCK_REALTIME`

`CLOCK_REALTIME` nhảy khi NTP chỉnh giờ hoặc đổi timezone ⇒ đo khoảng thời gian bằng wall-clock là
**bug**. Học kỹ ở W6.

### 4.6 `Threads::Threads` — đừng dựa vào tác dụng phụ

Ban đầu tôi dùng `Threads::Threads` mà không `find_package(Threads)`, ngầm dựa vào việc GoogleTest tự
gọi nó → CMake báo *"target was not found"*. **Lời gọi nội bộ của một dependency không phải contract
của nó.** Phải khai báo tường minh ở root `CMakeLists.txt`.

---

## 5. Khi nào KHÔNG dùng priority inheritance ⭐

1. **Nó không miễn phí.** Kernel phải theo dõi chuỗi thừa kế; mỗi lock/unlock đắt hơn. Đường không
   real-time thì trả giá vô ích.
2. **Không chống được deadlock.** PIP chỉ sửa *inversion*, không sửa *thứ tự lock sai*. Cần chống
   deadlock thì dùng PCP.
3. ⭐ **Nó là băng dán, không phải thuốc chữa.** Nếu đường real-time của bạn *cần* PIP, hãy hỏi lại:
   *tại sao đường này có mutex?* Câu trả lời tốt hơn thường là SPSC ring hoặc message passing.
4. **Chained blocking vẫn còn.** A chờ B chờ C — PIP truyền priority qua chuỗi, nhưng độ trễ cộng dồn
   vẫn phải tính vào WCET.

---

## 6. Câu hỏi phỏng vấn (→ `docs/interview/cpp_concurrency.md`)

1. Priority inversion là gì? Phân biệt **bounded** và **unbounded**.
2. Kể chuyện Mars Pathfinder. Task nào là thủ phạm, và tại sao nó bất ngờ?
3. Priority inheritance hoạt động thế nào? Nó **không** giải quyết được gì?
4. PIP vs PCP — khác nhau ở đâu, cái nào chống deadlock?
5. Trên Linux, bật priority inheritance bằng cách nào? Mặc định là gì?
6. ⭐ Tại sao một mutex trong đường hard real-time là vấn đề, **kể cả khi đã có PIP**?
7. `PTHREAD_EXPLICIT_SCHED` để làm gì? Quên nó thì chuyện gì xảy ra?
8. Cách nào **tốt hơn** cả PIP lẫn PCP? *(→ đừng chia sẻ tài nguyên)*

---

## 7. Nối vào capstone

- **W6** — đo jitter của đường IPC; priority inversion là một nguồn jitter
- **W12/W13** — "freedom from interference" ở mức **timing**, không chỉ mức memory
- Trong CDC thật: `can-service` (ưu tiên cao) và `hmi` (ưu tiên thấp) **không được** chia sẻ mutex ⇒
  đó là lý do capstone dùng SPSC ring / message passing thay vì shared state có lock
