# W38 — SPSC lock-free ring buffer

## 0. Bằng chứng

`SpscRing<T, Capacity>` — 6/6 test xanh trên **debug-20, debug-23, asan-23, ubsan-23, tsan-20, tsan-23**.
Stress test 1 producer + 1 consumer, 1,000,000 phần tử: FIFO đúng thứ tự, tổng khớp `N(N-1)/2`, **TSan im lặng** — cặp release/acquire chuẩn, không race trên payload.

| Test | Chốt |
|---|---|
| `PushPopIsFifo` | hợp đồng FIFO cơ bản |
| `PushFailsWhenFull` | đủ N slot (index vô hạn, không hy sinh slot nào) |
| `PopFailsWhenEmpty` | `out` không bị đụng khi thất bại |
| `WrapAroundReusesSlots` | 1000 vòng qua Cap=2, mask wrap đúng |
| `SupportsMoveOnlyType` | `unique_ptr` — placement-new + move, dtor dọn phần tử còn lại (ASan không leak) |
| `SingleProducerSingleConsumerStress` ⭐ | 1M phần tử, 2 thread, không lock, TSan sạch |

---

## 1. Vì sao SPSC lock-free được mà không cần CAS

- **Mỗi index có đúng 1 writer:** `tail_` chỉ producer ghi, `head_` chỉ consumer ghi. Không có "nhiều writer tranh nhau" → không cần `compare_exchange`/`fetch_add`, chỉ `store` thường.
- **Nhưng vẫn phải là `std::atomic`:** mỗi index bị thread *kia* đọc. Một `int` thường bị 1 thread ghi + 1 thread đọc = **data race = UB**. Compiler được phép cache vào register → consumer spin vĩnh viễn.
- ⭐ **Câu chốt:** *Atomic giải quyết visibility + tearing (2 thread chạm 1 biến). CAS giải quyết nhiều writer tranh nhau. SPSC bỏ được CAS, KHÔNG bỏ được atomic.*

## 2. Đặt memory_order ở đâu

```
push: construct payload  →  tail_.store(tail+1, release)     // release publish payload
pop : tail_.load(acquire)  →  đọc payload                    // acquire thấy payload
      head_.store(head+1, release)                           // release: slot đã free
push: head_.load(acquire)  →  kiểm tra full                  // acquire: thấy slot free, không đè
```

- **Owner đọc biến của mình = `relaxed`** (producer đọc `tail_`, consumer đọc `head_`): không cần đồng bộ với ai.
- **Cross-thread đọc = `acquire`**, ghép với **`release`** của thread kia → happens-before edge.
- Hai edge, hai chiều: 1 chiều **publish payload** (tail), 1 chiều **báo slot free** (head) để producer không đè lên slot consumer chưa đọc xong.

## 3. Index vô hạn (monotonic) + mask ⭐

- `head_`, `tail_` là `uint64_t` chạy tăng mãi, **không wrap**. `size = tail - head`.
- `empty ⇔ head == tail`; `full ⇔ tail - head == Capacity`. **Không mơ hồ** như kiểu wrap-index (ở đó `head==tail` vừa là đầy vừa là rỗng → phải hy sinh 1 slot).
- Chỉ mask khi *truy cập storage*: `slot(i) = storage[(i & (Cap-1))]`. Cần `Capacity` power-of-two → `& mask` thay `% Cap` (bỏ phép chia ~20-40 cycle trên hot path).
- Overflow của `uint64_t`? 2^64 phép push ở 1 tỷ/s ≈ 585 năm → bỏ qua.

## 4. False sharing — `alignas(64)`

Producer nện `tail_` mỗi push, consumer nện `head_` mỗi pop. Nếu 2 biến chung 1 cache line, mỗi ghi làm **invalidate** line của core kia → ping-pong giữa các core (nối lại W31). Tách mỗi index sang 1 cache line riêng bằng `alignas(kCacheLine)`.
Dùng hằng `64` tự định nghĩa thay `std::hardware_destructive_interference_size` vì GCC cảnh báo ABI-unstable dưới `-Werror`.

## 5. Khi nào KHÔNG dùng

- **>1 producer hoặc >1 consumer** → sai ngay (mất index, ghi đè). Cần MPMC (W39: Vyukov/Michael-Scott, CAS, ABA).
- Cần **blocking** (chờ khi rỗng/đầy) → cái này trả `false` non-blocking; muốn block thì phủ thêm `atomic::wait`/cv (nhưng mất tính "chỉ spin").
- `T` đắt để move, hoặc cần copy-on-pop → cân nhắc lại.
- Không cần hiệu năng cực đại → `std::queue` + `mutex` đơn giản hơn, ít bug hơn.

---

## 6. TỰ VIẾT

<!-- Tự tay viết lại từ đầu, không nhìn code, để retrieval:
     - Tại sao SPSC bỏ được CAS nhưng không bỏ được atomic?
     - Vẽ 2 happens-before edge (payload publish / slot free) và đặt release/acquire.
     - Vì sao index vô hạn tránh được mơ hồ đầy/rỗng?
     - False sharing xảy ra chính xác ở đâu, alignas sửa thế nào? -->

---

## 7. Câu hỏi phỏng vấn nối tiếp

1. Nếu bỏ `acquire` ở `pop` (dùng `relaxed` load `tail_`) thì hỏng gì? (consumer có thể thấy `tail` mới nhưng payload chưa visible → đọc rác)
2. Vì sao `push` cần `head_.load(acquire)` chứ không `relaxed`? (phải thấy consumer đã đọc xong slot trước khi tái sử dụng)
3. Batch: làm sao giảm số lần chạm atomic khi push/pop nhiều phần tử một lúc?
4. Cache 1 bản copy của `head`/`tail` phía đối diện (producer cache `head`) để giảm cross-core load — trade-off gì?
5. `uint64_t` vs `uint32_t` cho index: ABA/overflow ảnh hưởng ra sao?
6. Làm SPSC này thành blocking (chờ thay vì trả false) mà vẫn giữ lock-free fast-path?

**What would a Staff Engineer improve?**
- Cache `head`/`tail` phía đối diện thành biến non-atomic local, chỉ reload atomic khi cache báo full/empty → cắt phần lớn cross-core traffic (kiểu Folly `ProducerConsumerQueue`).
- API `try_push`/`try_pop` trả `std::optional`/`expected` thay `bool` + out-param; thêm `emplace` đã có sẵn.
- Cho phép capacity runtime (heap-allocated) thay compile-time, với allocator tùy biến (nối Phase 4).
- Benchmark throughput/latency (Google Benchmark) và đo cache-miss bằng `perf stat` để chứng minh padding có tác dụng.
