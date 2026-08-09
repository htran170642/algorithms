# W44 — SIMD · Autovectorization · `-march` · Đọc asm

## 0. Bằng chứng

Kernel `phase06_performance/src/vectorize_kernels.cpp` compile riêng, đọc `-fopt-info-vec` + `objdump`/`-S`. GCC 13.3, CPU AVX2 (không AVX-512). Test correctness xanh dưới `-20` và `-23`.

| Đo | Kết quả | Chốt |
|---|---|---|
| `-O2` (baseline) | **KHÔNG loop nào vectorize** | ⭐ `-O2` GCC dùng cost model *very-cheap* → bỏ qua loop cần versioning/epilogue |
| `-O3 -msse2` | 3 loop vectorize — "**16 byte** vectors" (`addps`, xmm, 4 float) | vectorize bật ở `-O3`, độ rộng = SSE2 |
| `-O3 -march=native` | "**32 byte** vectors" (`vaddps` `%ymm`, 8 float) | ⭐ `-march` (không phải `-O`) quyết định **độ rộng** SIMD |
| `add_aliased` @ -O3 native | **79 lệnh, 19 nhánh** | không restrict → runtime overlap guard + nhánh scalar fallback |
| `add_restrict` @ -O3 native | **56 lệnh, 11 nhánh** | ⭐ `__restrict` bỏ guard → **-30% code**, chỉ còn nhánh vectorized |
| `dot` @ -O3 native, **mặc định** | `vmulps` (nhân vector) **nhưng** `vaddss`/`vfmadd231ss` (cộng **scalar** in-order) | ⭐ nhân được vector-hoá, **tổng vẫn giữ thứ tự scalar** — không re-associate |
| `dot` @ -O3 native **+`-ffast-math`** | `vaddps` + `vfmadd231ps` (cộng **packed**) | ⭐ chỉ `-ffast-math` mới mở khoá **SIMD reduction** thật |

⭐ **Chốt đo được:** "loop vectorized" trong report **không** đồng nghĩa "nhanh 8x". Với `dot`, GCC vectorize *phép nhân* nhưng **từ chối re-associate phép cộng** (giữ `vaddss` scalar) để bảo toàn ngữ nghĩa float — đúng như dự đoán, nhưng tinh vi hơn "không vectorize": nó vectorize *một phần*.

---

## 1. Aliasing — kẻ chặn autovectorization số 1 ⭐

`out[i] = a[i] + b[i]` với con trỏ trần: compiler **không chứng minh được** `out` không chồng `a`/`b`. Nếu `out == a+1`, ghi `out[i]` sửa `a[i+1]` mà lần sau đọc → vectorize (8 phần tử/lần) sai. Nên GCC phát **loop versioning**:

```
so sánh range [out, out+n) với [a,a+n),[b,b+n)  →  disjoint ? nhánh SIMD : nhánh scalar
```

- Đo được: `add_aliased` = 79 lệnh/19 nhánh (guard + 2 nhánh), `add_restrict` = 56/11. `__restrict` = lời hứa "không chồng" → GCC bỏ guard.
- ⚠️ `__restrict` là **hợp đồng bạn ký**: nếu thực tế chúng chồng nhau → **UB**, không phải warning. Chỉ dùng khi *chắc chắn*.
- Ngoài restrict, cái chặn khác: bước nhảy không liền (`a[i*stride]`), phụ thuộc vòng lặp (`a[i]=a[i-1]+..`), rẽ nhánh trong loop, gọi hàm không inline.

## 2. `-O` và `-march` vuông góc nhau ⭐

| Trục | Điều khiển | Đo được |
|---|---|---|
| **Có vectorize không** | thuật toán autovec | `-O2` GCC13 = *very-cheap* → ở đây **không** vectorize; `-O3` mới bật |
| **Vectorize rộng bao nhiêu** | tập lệnh SIMD được phép phát | `-msse2` = 16B (4 float); `-march=native`/`-mavx2` = 32B (8 float) |

- Đính chính so với "textbook `-O2` đã vectorize": GCC ≥12 *bật* vectorizer ở `-O2` nhưng với **cost model very-cheap** — chỉ nhận loop không cần versioning/epilogue. Ba kernel này đều cần → `-O2` bỏ hết. `-O3` (hoặc `-O2 -fvect-cost-model=cheap`) mới ăn. **Đo, đừng nhớ.**
- `-march=native` mở AVX2 nhưng binary **không portable** (chạy CPU cũ → `SIGILL`). Production: chọn baseline cụ thể (`-march=x86-64-v3`) hoặc multi-versioning (`__attribute__((target_clones))`).

## 3. Reduction float & non-associativity ⭐

`sum += a[i]*b[i]`: `(a+b)+c ≠ a+(b+c)` với float (làm tròn khác). Gộp vào 8 lane rồi fold = đổi thứ tự cộng → **lệch bit**. Nên:
- **Mặc định:** GCC vectorize phần nhân (`vmulps`) nhưng cộng dồn **scalar in-order** (`vaddss`) → kết quả *bit-for-bit* giống scalar, **không** speedup ở phần tổng.
- **`-ffast-math`:** cho phép re-associate → `vaddps` (cộng 8 lane song song) + horizontal fold cuối → **SIMD reduction thật**, nhưng kết quả có thể lệch ULP.
- Đây là lý do `dot`/`norm`/`mean` thường "không nhanh như mong đợi" nếu không `-ffast-math`. Thay thế an toàn hơn `-ffast-math` toàn cục: `#pragma omp simd reduction(+:sum)` (opt-in cục bộ), hoặc `-fassociative-math -fno-trapping-math` gọn hơn.

## 4. Roofline — SIMD chỉ cứu compute-bound

- Speedup ~lane-count chỉ đạt khi bottleneck là **ALU**. Mảng >> cache → **memory-bandwidth bound**, CPU chờ RAM, SIMD vô ích.
- **Arithmetic intensity** = FLOP / byte nạp. `sum += a[i]` ≈ 1 FLOP / 4 byte → cực thấp → memory-bound. Nhân ma trận nhỏ vừa cache → cao → compute-bound → SIMD ăn đủ.
- Kết luận nghề: **đo speedup thật bằng benchmark**, đừng nhân lane-count. "8 lane" là trần lý thuyết, không phải kỳ vọng.

## 5. Đọc asm — công cụ, không phải phép thuật

- `-fopt-info-vec-optimized` (cái gì vectorize) / `-vec-missed` (vì sao trượt) / `-vec-all` (đủ). Nhanh hơn đọc asm để biết *có* vectorize không.
- `g++ -O3 -S` hoặc `objdump -d` + Godbolt để xem *lệnh gì*: `addps`/`mulps` (xmm=4) vs `vaddps`/`vfmadd*ps` (ymm=8) vs đuôi `ss`=scalar.
- Giữ hàm ở **TU riêng, external linkage** để không bị inline/DCE mất — nếu không compiler xoá cả loop khi kết quả không dùng (`DoNotOptimize` của W41 là để chống chính điều này trong benchmark).

---

## 6. TỰ VIẾT

<!-- Tự tay viết lại, không nhìn:
     - Vì sao con trỏ trần chặn vectorize? __restrict làm gì, đánh đổi (UB) là gì?
     - -O và -march khác vai trò thế nào? -O2 GCC13 có vectorize 3 kernel này không, vì sao?
     - dot mặc định: phần nào vector-hoá, phần nào scalar, vì sao? -ffast-math đổi gì (vaddss->vaddps)?
     - Roofline: khi nào SIMD vô ích? arithmetic intensity của sum+=a[i] là bao nhiêu?
     - 3 cờ -fopt-info-vec-*; cách phân biệt xmm/ymm và ps/ss trong asm. -->

---

## 7. Câu hỏi phỏng vấn nối tiếp

1. Vì sao `float* out, const float* a` chặn autovectorization còn `int` index thì không? (compiler phải giả định `out` có thể alias `a` → versioning; restrict/local buffer gỡ được)
2. `-O2` với `-O3` khác gì về vectorization trên GCC hiện đại? (đều *bật* vectorizer, nhưng `-O2` cost model very-cheap bỏ loop cần versioning/epilogue → thực tế cần `-O3`)
3. `-O3` rồi mà asm vẫn chỉ thấy `addps` xmm, không thấy `vaddps` ymm — thiếu gì? (`-march`/`-mavx2`; `-O` không chọn tập lệnh)
4. Vì sao `sum += a[i]` không tự vectorize thành SIMD reduction? Đổi bằng cách nào? (float non-associative; `-ffast-math`/`-fassociative-math` hoặc `#pragma omp simd reduction`)
5. Vectorize hoàn hảo mà chỉ nhanh 1.1x — vì sao? (memory-bandwidth bound; arithmetic intensity thấp → Roofline)
6. `-march=native` build xong chạy máy khác `SIGILL` — vì sao, sửa sao? (phát AVX2 mà CPU đích không có; dùng baseline `x86-64-v2/v3` hoặc `target_clones` multiversioning)

**What would a Staff Engineer improve?**
- Thêm **benchmark thật** (Google Benchmark, W41) đo scalar-vs-SIMD trên mảng vừa-cache vs quá-cache — biến Roofline từ lời nói thành số, chứng minh khi nào SIMD *thực sự* đáng.
- CI gắn `-fopt-info-vec-missed` + grep hot loop: nếu commit nào làm loop nóng **mất vectorize** (thêm alias, thêm nhánh), fail build — "vectorization regression guard".
- Không rải `-ffast-math` toàn dự án (đổi ngữ nghĩa NaN/Inf/thứ tự, gây bug tài chính/khoa học). Khoanh vùng bằng `#pragma omp simd` hoặc `[[gnu::optimize]]` cho đúng kernel cần.
- Với data-parallel nặng: cân nhắc **intrinsics** (`<immintrin.h>`) hoặc thư viện (`std::simd`/Highway/xsimd) khi autovec không ổn định — kèm test số học so với scalar reference (đúng như test tuần này).
- Chọn `-march=x86-64-v3` (AVX2 phổ cập ~2015+) thay `native` để cân bằng tốc độ và tính portable của binary ship ra ngoài.
