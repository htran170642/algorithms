# W42 — CPU cache · branch prediction · `perf stat`

## 0. Bằng chứng

`bench_branch_predict` (release-23), 1M byte 0..255, `sum += v khi v>=128`, `--benchmark_repetitions=3`:

| Benchmark | Time | items/s | Cái gì xảy ra |
|---|---|---|---|
| `BM_Branch_Random` (branch thật, random) | 3.26 ms | 321 M/s | predictor đoán ~50% sai → flush pipeline |
| `BM_Branch_Sorted` (branch thật, sorted) | 0.479 ms | 2.19 G/s | ⭐ **6.8× nhanh hơn** — cùng phép cộng, chỉ khác thứ tự |
| `BM_Cmov_Random` (plain -O2, random) | 0.521 ms | 2.01 G/s | GCC if-convert → `cmov`, KHÔNG còn branch để đoán |
| `BM_Mask_Random` (branchless tay) | 0.487 ms | 2.15 G/s | mask `-(v>=128)` — đúng thứ `cmov` sinh ra |

⭐ **Hai chốt đo được:**
1. **Branch mispredict là thật:** ép branch sống sót thì random chậm **6.8×** so với sorted — cùng công việc, khác mỗi *độ đoán được* của nhánh.
2. **Compiler hiện đại XOÁ demo này:** ở `-O2` GCC 13 biến `if (v>=128) sum+=v` thành **`cmov`** (branchless). `BM_Cmov_Random` chạy random mà nhanh **6.3×** so với `BM_Branch_Random` — *fix không phải sort dữ liệu, mà là bỏ branch.* Muốn *nhìn thấy* branch predictor phải tắt if-conversion (`__attribute__((optimize("no-if-conversion",...)))`).

Xác minh asm (scratch): hàm có attribute giữ **1 conditional jump**; hàm plain -O2 có **1 `cmov`, 0 jump**. Cùng source, khác code sinh ra.

> `perf stat -e branch-misses` bị chặn: `perf_event_paranoid = 4` (cần ≤ 2). Timing đã chứng minh hiệu ứng; perf chỉ *xác nhận cơ chế*. Chạy khi có quyền:
> ```
> sudo sysctl -w kernel.perf_event_paranoid=1
> BIN=./build/release-23/bench/bench_branch_predict
> perf stat -e instructions,cycles,branches,branch-misses $BIN --benchmark_filter=BM_Branch_Random
> perf stat -e instructions,cycles,branches,branch-misses $BIN --benchmark_filter=BM_Branch_Sorted
> ```
> Kỳ vọng: `branch-misses` ~50% (Random) vs ~0.1% (Sorted); IPC tụt theo.

---

## 1. Kim tự tháp trễ — Big-O giấu hằng số 100×

| Tầng | Trễ ~ | Nếu L1 = 1 giây |
|---|---|---|
| L1 | ~1 ns (4 cycle) | 1 giây |
| L2 | ~4 ns | 4 giây |
| L3 | ~15–40 ns | ~30 giây |
| **DRAM** | **~100 ns (200–300 cycle)** | **~2 phút** |

- Miss ra DRAM ≈ **100×** L1 hit. "O(n) cache-friendly" đánh bại "O(n) nhảy lung tung" — đây là *why* của W31 (AoS/SoA) và W41 (padding).
- Đo bằng `perf stat -e cache-references,cache-misses,LLC-load-misses`; tỉ lệ `cache-misses/cache-references` = mức độ stall vì RAM.

## 2. Branch prediction — máy đoán mà bạn có thể bỏ đói

- Pipeline sâu 15–20 stage → CPU **không chờ** biết `if`, nó *đoán* rồi chạy speculative.
- Đoán đúng: 0 phí. Đoán **sai**: xả pipeline → phạt **~15–20 cycle**.
- **Sorted**: nhánh sai một mạch dài rồi đúng một mạch dài → predictor học → ~100% đúng.
- **Random**: tung đồng xu → ~50% sai → mỗi 2 phần tử một flush → chậm 3–6×.
- ⭐ Không phải cache: cả hai duyệt tuần tự y hệt. Khác biệt **thuần branch**.

## 3. Vì sao demo cổ điển "hết hiệu lực" — if-conversion ⭐

- `-O2` chạy **if-conversion**: nhánh ngắn, không side-effect → biến thành **`cmov`** (conditional move) hoặc mask vector hoá. `cmov` không phải jump → **không có gì để mispredict**.
- Nên `BM_Cmov_Random` (random!) nhanh ngang `BM_Branch_Sorted`. Bài học 2020s: *đừng sort để cứu nhánh — hãy để (hoặc giúp) compiler bỏ nhánh.*
- Ép branch sống để dạy học: `__attribute__((optimize("no-if-conversion","no-if-conversion2","no-tree-loop-if-convert","no-tree-vectorize")))` — tắt if-conversion + vector hoá **chỉ hàm đó**, phần còn lại vẫn -O2.
- Khi NÊN tự branchless: nhánh **data-dependent + khó đoán** trên hot path. Khi KHÔNG: nhánh dễ đoán (predictor ~100% rồi) → `cmov` có khi *chậm hơn* vì mất speculation + luôn tính cả 2 vế.

## 4. `perf stat` — đọc sự thật phần cứng (PMU)

```
perf stat -e instructions,cycles,cache-references,cache-misses,branches,branch-misses ./prog
```

- `branch-misses / branches` → % đoán nhánh sai.
- `cache-misses / cache-references` → stall vì RAM.
- **IPC = instructions/cycles**: <1 = CPU đói (đang chờ); >2 = chạy mượt. Mispredict/cache-miss kéo IPC xuống.
- ⭐ Biến giả thuyết thành bằng chứng: đây là cách *chứng minh* "+8% padding của W41 là do giảm cache bouncing", không phải đoán.

## 5. Khi nào KHÔNG tin/KHÔNG áp dụng

- **Microbenchmark isolate làm nóng cache/BTB giả tạo** → predictor "học thuộc" pattern mà production không có. Số đẹp giả.
- **Compiler đã branchless hoá** → tối ưu tay thành thừa; luôn đọc asm (`-S`) hoặc `perf` trước khi tin lời mình.
- **Governor không pin** (`CPU scaling is enabled`) → nhiễu; đọc mean±stddev, `cpupower frequency-set -g performance`.
- **Amdahl**: nhánh khó đoán nhưng chiếm 0.1% wall-clock → tối ưu vô nghĩa. **Profile trước.**
- Branchless che **timing side-channel** (crypto) là lý do *đúng đắn* khác để bỏ nhánh — nhưng đó là bảo mật, không phải tốc độ.

---

## 6. TỰ VIẾT

<!-- Tự tay viết lại, không nhìn:
     - Bảng trễ L1→DRAM và con số "miss ≈ 100× hit".
     - Vì sao sorted nhanh hơn random dù cùng phép tính? Cơ chế pipeline flush.
     - Vì sao ở -O2 GCC làm gap biến mất? if-conversion → cmov là gì?
     - Khi nào tự branchless CÓ lợi, khi nào cmov CHẬM hơn branch?
     - 3 tỉ số quan trọng trong perf stat (branch-miss%, cache-miss%, IPC). -->

---

## 7. Câu hỏi phỏng vấn nối tiếp

1. Sorted nhanh hơn random 6.8× — chứng minh là do branch chứ không do cache thế nào? (cả hai duyệt tuần tự y hệt; `perf` cho `cache-misses` gần bằng nhau, `branch-misses` chênh 500×)
2. `cmov` luôn tốt hơn nhánh? (không — nhánh **dễ đoán** để CPU speculate qua, `cmov` buộc tính cả 2 vế + tạo data-dependency chặn ILP → chậm hơn)
3. `perf stat` báo IPC=0.4. Suy ra gì, kiểm tra tiếp counter nào? (CPU đói → xem branch-misses và cache-misses để biết đói vì cái gì)
4. Vì sao microbenchmark predictor dễ cho số lạc quan? (BTB học thuộc pattern lặp; production nhánh phân tán hơn)
5. `__builtin_expect`/`[[likely]]`/`[[unlikely]]` giúp gì và KHÔNG giúp gì? (gợi ý layout code + đoán tĩnh; vô dụng khi nhánh thật sự 50/50 — predictor động vẫn thắng)
6. Loop unrolling và branch có liên hệ gì? (giảm số lần kiểm tra điều kiện vòng lặp → ít branch predictable, nhưng đó là nhánh dễ đoán nên lợi ít)

**What would a Staff Engineer improve?**
- Hạ `perf_event_paranoid` (hoặc `CAP_PERFMON`) rồi đính **`branch-misses` thực đo** vào note — biến 6.8× từ *suy luận* thành *counter*.
- Quét `->Arg()` nhiều mức "độ đoán được" (0%, 10%, 50%, 90% phần tử ≥128) để vẽ đường cong mispredict→time, không chỉ 2 điểm.
- Đo cả 3 tầng: thêm variant **cache-miss-bound** (mảng > L3, truy cập random) tách bạch "chậm vì branch" với "chậm vì RAM".
- Dùng `perf record`/`perf annotate` chỉ đúng dòng asm mispredict, đính flamegraph.
- Cân nhắc PGO (`-fprofile-use`): để compiler tự đặt layout nhánh theo profile thật thay vì đoán tĩnh.
