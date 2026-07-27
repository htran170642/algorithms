# W31 — Cache Locality (AoS vs SoA) + False Sharing

> Bench: [`bench/bench_aos_soa.cpp`](../../bench/bench_aos_soa.cpp) · [`bench/bench_false_sharing.cpp`](../../bench/bench_false_sharing.cpp)
> Mục **7** để trống — tự viết. Đóng **Phase 4**.

---

## 0. Bằng chứng (release-23, số đo thật)

```
AoS vs SoA  (1M particle, hot loop chỉ đụng x,y,z,vx,vy,vz = 24/64 byte):
    BM_AoS   9 514 442 ns   |   BM_SoA   1 993 871 ns   →  SoA nhanh ~4.8×

False sharing (4 thread, mỗi thread ++ counter riêng, 20M lần):
    BM_Packed  1017 ms      |   BM_Padded  36.8 ms      →  padded nhanh ~27×
```

> `perf stat -e cache-misses,LLC-load-misses` bị chặn: `perf_event_paranoid=4`.
> Mở bằng `sudo sysctl kernel.perf_event_paranoid=1` rồi chạy lại — cache-miss là
> bằng chứng *trực tiếp* cho câu chuyện dưới đây (wall-clock là bằng chứng *gián tiếp*).

---

## 1. ⭐ AoS vs SoA — quyết định bởi ACCESS PATTERN, không phải "field liền nhau"

Bẫy trực giác: "mọi field của object nằm liền nhau ⇒ cache thích". Đúng khi bạn đụng **hầu hết field của MỘT** object. Sai khi bạn **quét vài field trên NHIỀU** object.

Phép tính byte (bài toán này): `Particle` = 64 byte = **1 cache line**. Hot loop chỉ đọc/ghi `x,y,z,vx,vy,vz` = **24 byte hữu ích**.

| | Nạp / line | Byte hữu ích / line | Lãng phí băng thông |
|---|---|---|---|
| **AoS** `vector<Particle>` | 64 byte (1 hạt) | 24 | **~63%** kéo `cold[10]` vô ích |
| **SoA** `x[],y[],z[],…` | 64 byte (16 float của 1 mảng) | 64 | **0%** — line toàn byte hữu ích |

SoA nạp ~2.6× ít line hơn cho pha xyz ⇒ ít `LLC-load-misses` ⇒ nhanh. Đo được **4.8×**, cao hơn 2.6× vì SoA còn **auto-vectorize**: `x[i..i+7]` liền khối nạp thẳng thanh ghi SIMD, khỏi gather.

---

## 2. ⭐ "It depends" — khi AoS thắng ngược lại

SoA **không** luôn thắng. Câu trả lời Senior là *"phụ thuộc access pattern, đo đừng đoán"*:

| Truy cập | Thắng | Vì sao |
|---|---|---|
| Quét **1–vài field** trên **nhiều** phần tử (physics, filter cột) | **SoA** | line 100% hữu ích, SIMD-friendly |
| Đụng **hầu hết field của MỘT** phần tử (`render(entity[i])`) | **AoS** | cả object 1 line; SoA phải chạm 8 mảng ⇒ 8 line, 8 miss |
| Thêm/xóa cả phần tử liên tục | **AoS** | SoA phải sửa song song N mảng, dễ lệch |

Đây là kiến trúc **DOD (data-oriented design)** trong game/HPC/columnar DB. `std::vector` của mỗi cột = SoA; một Parquet/Arrow column = SoA ở tầng lưu trữ.

---

## 3. ⭐ False sharing — hai biến, hai thread, VẪN chậm 27×

Không phải data race (không sai kết quả, không cần mutex). Cơ chế **MESI cache coherence**:

- Đơn vị đồng bộ giữa các core là **cả cache line 64 byte**, không phải từng biến.
- Hai counter cạnh nhau ⇒ cùng một line. Thread A ghi ⇒ line ở core B bị **Invalidate**; thread B ghi ⇒ line ở core A bị Invalidate. Line **ping-pong** qua bus coherence mỗi lần ghi.
- Kết quả: hai thread "độc lập" chạy như thể **nối tiếp + phí đồng bộ** — đo được **27× chậm**.

Chẩn đoán trên perf: HITM cao (`perf c2c`), hoặc `cache-misses` tăng vọt dù dữ liệu bé.

---

## 4. ⭐ Cách sửa false sharing = SPATIAL, không phải logical

Đẩy mỗi biến nóng sang line riêng:

```cpp
struct alignas(64) Cell { std::atomic<int64_t> v; };   // 64 = 1 line riêng
Cell counters[N];                                       // mỗi counter 1 line
```

- `alignas(64)` (chuẩn hơn: `std::hardware_destructive_interference_size`) chỉ **NỚI RỘNG** align + độn `sizeof` lên 64 (nối thẳng W27: `alignas` chỉ widen, thêm padding). Đổi RAM lấy tốc độ.
- **Không** dùng `volatile` (không liên quan coherence) hay `mutex` (thêm contention, sai bệnh).
- Mặt trái của cùng đồng xu: `std::hardware_constructive_interference_size` — khi bạn *muốn* hai thứ **cùng** line (đọc chung, đóng gói cho locality).

---

## 5. Sai lầm phổ biến ngoài production

- Mảng per-thread counter/accumulator packed liền → false sharing âm thầm giết scalability (thêm core mà không nhanh hơn).
- `struct { std::mutex m; int hot_counter; }` cạnh nhau nhiều instance trong 1 mảng → hot_counter của các instance chia line.
- Head/tail của SPSC ring buffer chung line → producer & consumer ping-pong (sẽ gặp lại **W38**).

---

## 6. Chuẩn C++ liên quan

- `std::hardware_destructive_interference_size` / `..._constructive_...` (C++17, `<new>`) — hằng số line, nhưng GCC cảnh báo `-Winterference-size` khi dùng làm `alignas` (ABI có thể đổi) → dự án hardcode 64.
- `[[no_unique_address]]` (W27) — hướng ngược: gộp field rỗng, tiết kiệm chứ không tách.
- `std::mdspan` (C++23) — view đa chiều, có thể map layout AoS/SoA mà không đổi thuật toán.

---

## 7. TỰ VIẾT (retrieval — tự điền, không nhìn phía trên)

> Đề: **Cho vòng lặp chỉ cập nhật vị trí của 1 triệu hạt, giải thích vì sao SoA nạp ít cache line hơn AoS (tính theo byte hữu ích/line), một trường hợp AoS thắng ngược lại, và vì sao false sharing chậm dù không có data race — kèm cách sửa.** 5–6 câu.

_(câu trả lời của bạn ở đây)_

---

## 8. Câu hỏi phỏng vấn nối tiếp

- SoA làm code khó đọc/khó thêm-xóa entity hơn. Khi nào cái giá đó KHÔNG đáng? (gợi ý: object ít, hoặc luôn đụng cả struct)
- `perf c2c` báo gì mà `perf stat` không? (gợi ý: định vị *dòng nào* bị HITM, *thread nào* đụng độ)
- `alignas(64)` mỗi counter tốn 56 byte padding/counter. Với 1 triệu counter thì sao? Có cách khác không? (gợi ý: chỉ pad thứ *ghi nóng đa luồng*, gộp per-thread rồi reduce)
- `hardware_destructive_interference_size` trên máy có L2 prefetch cặp-line (128 byte hiệu dụng) thì 64 có đủ? (gợi ý: một số Intel prefetch 2 line)
- AoS→SoA đổi cả API. Làm sao giữ interface `Particle&` mà lưu SoA bên dưới? (gợi ý: proxy/reference object, `std::mdspan`, hoặc `soa_vector` kiểu EnTT)
- Cache line ping-pong khác gì với *true* sharing (một biến thật sự dùng chung)? Cách sửa có giống nhau không?
