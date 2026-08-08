# W41 — Benchmarking discipline · Google Benchmark · `DoNotOptimize`

## 0. Bằng chứng

`bench_spsc_ring` (release-23), 4M item/iter, 1 producer + 1 consumer, `--benchmark_repetitions=5`:

| Benchmark | Time (mean) | items/sec (mean ± stddev) |
|---|---|---|
| `BM_SpscRing_Real` (component thật W38) | 41.0 ms | **97.8M/s** ± 3.6M |
| `BM_MiniRing_Padded` (`alignas(64)`) | 41.8 ms | 95.9M/s ± 4.3M |
| `BM_MiniRing_Packed` (index dồn 1 line) | 45.0 ms | 88.9M/s ± 0.3M |

⭐ **Chốt đo được:** `alignas(64)` trên `head_/tail_` cho **~+8% throughput** trên ring thật — payoff *thật* nhưng **khiêm tốn**, KHÔNG phải 3–10x như counter tổng hợp (`bench_false_sharing`, W31). Lý do: mỗi op ring còn làm buffer store/load + số học index, nên tranh cache-line trên 2 atomic chỉ chiếm **phần nhỏ** tổng công. `SpscRing` thật (97.8M) bám sát MiniRing padded (95.9M) → trừu tượng `std::launder`/placement-new gần như **free**.
Google Benchmark in `***WARNING*** CPU scaling is enabled` — đúng bài học tuần này: turbo/governor làm nhiễu, phải đọc **mean ± stddev**, không đọc 1 lần chạy.

---

## 1. Ba cái bẫy — microbenchmark là môn chống lại compiler + CPU

| Bẫy | Ai gây | Vũ khí |
|---|---|---|
| **Dead-code elimination** | compiler xoá code không ai đọc | `benchmark::DoNotOptimize` / `ClobberMemory` |
| **Đo build sai** | `-O0` ≠ production (không inline/RVO) → thứ hạng đảo ngược | benchmark ở **release** (`-O2/-O3`) |
| **Nhiễu + first-iteration** | timer resolution, scheduler, turbo | GBench tự chọn iteration; setup **ngoài** vòng `state`; đọc stddev; pin governor |

## 2. `DoNotOptimize` — cơ chế

```cpp
long sum = std::accumulate(v.begin(), v.end(), 0L);
benchmark::DoNotOptimize(sum);   // asm rỗng: "sum đã escape, tao có thể đọc"
```

- Là inline-asm rỗng báo compiler giá trị đã **escape** → không được xoá phép tính tạo ra nó. Chặn DCE.
- **Không** tắt optimizer chỗ khác (khác hẳn `-O0`). Chỉ đóng đinh đúng biến bạn truyền.
- `ClobberMemory()` mạnh hơn: "toàn bộ memory có thể đã đổi" → chặn cả store bị elide (dùng khi kết quả nằm trong buffer, không phải scalar).
- ⭐ Bằng chứng sống: `bench_all.cpp` có cặp `BM_SumButOptimizedAway` (0 ns — bị xoá) vs `BM_SumMeasuredHonestly` (số thật). Khoảng cách *chính là* bài học.

## 3. Benchmark có nhiều thread — 2 thứ phải đúng

- **`UseRealTime()`**: mặc định GBench đo **CPU time** = tổng thời gian mọi thread. Với 1P-1C ta muốn **wall-clock** → phải khai báo, không thì số vô nghĩa.
- **`SetItemsProcessed(n)`**: report **items/sec** — metric có nghĩa cho queue — thay vì ns/iter của vòng lặp ngoài mờ mịt. GBench tự chia cho thời gian đo được.
- **Setup ngoài vòng `for (auto _ : state)`**: chỉ thân vòng bị bấm giờ. Dựng ring / dữ liệu test đặt trước vòng → không tính vào phép đo. (Ở đây ring dựng *trong* vòng vì mỗi iter cần ring mới sạch — chi phí ctor nhỏ so với 4M op, chấp nhận được và cố ý.)

## 4. Tại sao GBench tự chọn số iteration

`std::chrono` có độ phân giải hữu hạn (~20–100 ns) + scheduler cướp core → đo 1 lần hàm 2 ns = rác. GBench **tăng iteration** đến khi tổng thời gian đủ lớn cho thống kê ổn định (hàm càng nhanh chạy càng nhiều vòng). Bạn **không** fix cứng `for i in 0..1e6`. `--benchmark_repetitions=N` chạy lại toàn bộ N lần → `mean/median/stddev`. Đọc **stddev**: `Packed` stddev 0.3M (ổn định) vs `Padded` 4.3M (nhiễu hơn vì bám turbo) → chênh 8% vẫn vượt nhiễu, kết luận đứng vững.

## 5. Khi nào KHÔNG tin/KHÔNG dùng microbenchmark

- **Governor không pin `performance`** → turbo scaling làm số nhảy; GBench cảnh báo `CPU scaling is enabled`. Production: `cpupower frequency-set -g performance` hoặc chạy nhiều repetitions + đọc median.
- Đo **cache/branch** thì microbenchmark isolate làm nóng cache giả tạo → dùng **`perf stat`** (W42) cho counter phần cứng thật.
- Micro nhanh ≠ macro nhanh: tối ưu 1 hàm 8% vô nghĩa nếu nó chiếm 0.1% wall-clock → **profile trước** (Amdahl), benchmark sau.
- Máy đang bận (load average 5.74 ở lần chạy này) → nhiễu; benchmark khi máy rảnh.

---

## 6. TỰ VIẾT

<!-- Tự tay viết lại, không nhìn:
     - 3 bẫy microbenchmark và vũ khí chặn từng cái.
     - DoNotOptimize làm gì ở mức asm? Khác gì -O0?
     - Vì sao threaded benchmark cần UseRealTime()? Đo sai gì nếu quên?
     - Tại sao padding trên SpscRing chỉ +8% mà trên counter tổng hợp là 3-10x? -->

---

## 7. Câu hỏi phỏng vấn nối tiếp

1. `DoNotOptimize(x)` vs `ClobberMemory()` — khi nào phải dùng cái sau? (kết quả ghi vào buffer/memory chứ không phải scalar trong register)
2. Benchmark của bạn cho A nhanh hơn B ở `-O2` nhưng chậm hơn ở `-O0`. Con số nào đáng tin, vì sao?
3. Vì sao GBench báo `CPU scaling enabled` là vấn đề, và 2 cách xử lý?
4. Đo throughput 1P-1C mà quên `UseRealTime()` → số bị lệch theo hướng nào? (CPU time gộp 2 thread → ~2x thời gian → throughput nhìn như một nửa)
5. `state.range(0)` + `->Range(8, 8<<10)` dùng để làm gì? (quét kích thước input, vẽ đường cong complexity thực nghiệm)
6. Padding chỉ +8% trên ring thật — có đáng giữ `alignas(64)` (tốn 128B/ring) không? Trade-off?

**What would a Staff Engineer improve?**
- Thêm **`->Threads()`**/`->Range()` quét capacity & payload size để vẽ đường cong, không chỉ 1 điểm.
- Chạy dưới **`perf stat -e cache-misses,LLC-load-misses`** (W42) để chứng minh +8% *là* do giảm cache-line bouncing, không phải may rủi turbo.
- Pin thread vào core cố định (`pthread_setaffinity_np`) + governor `performance` → tách biến, giảm stddev.
- Report **latency phân vị** (p50/p99), không chỉ throughput trung bình — queue thực tế quan tâm đuôi.
- So với baseline `moodycamel::ReaderWriterQueue`/Folly `ProducerConsumerQueue` để biết ring của mình cách "state of the art" bao xa.
