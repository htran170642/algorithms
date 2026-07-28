# Design Round #4 — In-Memory Particle / Entity Store

**Boundary:** cuối Phase 4 (W31)
**Đề:** giữ 1–10 triệu entity cho engine 60 FPS. Mỗi frame (~16 ms): (1) **update physics**
`pos += vel·dt` trên *toàn bộ*, (2) **spawn/despawn** liên tục, (3) **radius query** quanh
điểm P cho collision. Chạy đa lõi.

Đây là toàn bộ Phase 4 ghép lại: layout AoS/SoA (W27, W31), arena (W29), pool (W30),
false sharing (W31). Theo System Design Workflow của CLAUDE.md (11 bước).

---

## 1–2. Requirements & Clarifications

| Câu hỏi làm rõ | Chốt | Vì sao đổi thiết kế |
|---|---|---|
| Tần suất mỗi thao tác? | physics **mỗi frame/toàn bộ**; spawn/despawn vài nghìn/frame; query **mỗi frame** | Cái chạy mỗi-frame-toàn-bộ quyết định layout → **SoA** cho hot fields. Query mỗi frame → cần **spatial index**, không brute-force O(n²) |
| Thao tác nào đọc *field nào*? | physics: `pos,vel`. render: `pos+màu+model`. AI: `pos+health+flags` | `pos` nóng mọi hệ; `vel` chỉ physics; còn lại lạnh → **hybrid layout**, không SoA-tất-cả cũng không AoS-tất-cả |
| Ngân sách latency? | **16 ms cứng**, jitter tối kỵ, **không** cấp phát HĐH giữa frame | Loại `new/delete` trong frame (page fault → jitter). Pre-allocate + **pool** + **arena** |
| Vòng đời entity? | đạn/quái **cùng cỡ, sống-chết lẻ**; buffer va chạm **sống trọn 1 frame rồi bỏ** | Hai pattern → **hai allocator**: pool (W30) cho entity lẻ, arena (W29) cho buffer tạm |
| Định danh: con trỏ hay handle? | hệ khác giữ **handle/ID**, không giữ con trỏ | Được **nén mảng** khi despawn → giữ SoA dày đặc. Nhưng cần **generation** chống stale handle |
| Đa lõi ghi ra sao? | physics chia **range cho N thread**; spatial index build lại mỗi frame | Mỗi thread một range SoA riêng → coi chừng **false sharing** ở biên + per-thread counters (pad `alignas(64)`) |

## 3. Constraints

- Physics update: **O(n)** thuần, cache-friendly, song song N thread, **16 ms cứng**.
- Spawn/despawn: **O(1)**, không cấp phát HĐH giữa frame.
- Radius query: sub-linear (grid/BVH), không O(n²).
- Handle ổn định qua nén mảng; despawn không làm hỏng handle của entity khác.
- Bộ nhớ: 10M entity, chỉ pad thứ *ghi nóng đa luồng* (không pad tràn lan).

## 4. ⭐ Architecture — hybrid layout

Quyết định cốt lõi: **KHÔNG** một layout cho tất cả. Chia theo *access pattern* (W31: "đo access pattern, đừng đoán").

```
                 handle (id + generation)
                        │  O(1)
                        ▼
   sparse[]  ───────►  dense_index         (sparse-set: handle → chỗ trong SoA)
                        │
        ┌───────────────┼───────────────────────────┐
        ▼               ▼                             ▼
   HOT-SoA (physics)  WARM-SoA (mọi hệ đọc)      COLD-AoS (lạnh, ít quét)
   vx[] vy[] vz[]     x[] y[] z[]                struct Cold{ color; model*;
   (chỉ physics)      (render+AI+physics đọc)          health; flags; ... }[]
```

- **HOT-SoA** `vx,vy,vz` — chỉ physics đụng. SoA → line 100% hữu ích, auto-vectorize (W31 đo 4.8×).
- **WARM-SoA** `x,y,z` — *mọi* hệ đọc `pos`. Tách riêng để physics quét `x[]` không kéo `vel` hay cold.
- **COLD-AoS** — field lạnh gom thành struct, quét hiếm; giữ AoS vì khi *đụng* thì thường đụng cả cụm (render đọc màu+model cùng lúc).

> **Layout không phải chọn một; là phân vùng theo ai-đọc-gì-bao-nhiêu.**

## 5. Trade-offs

| Quyết định | Được | Mất |
|---|---|---|
| Hybrid SoA/AoS thay vì AoS thuần | physics 4.8× nhanh, SIMD | code phức tạp, thêm/xóa entity phải sửa nhiều mảng song song |
| Sparse-set (handle→dense) | despawn O(1) **swap-and-pop**, SoA luôn dày đặc, iterate cache-hot | thêm 1 lớp gián tiếp (sparse[] tra cứu), tốn RAM cho sparse |
| Pool cho entity + arena cho tạm | không cấp phát HĐH trong frame, O(1) | phải biết trước upper-bound, hoặc grow theo chunk (W30) |
| Grid spatial index build lại mỗi frame | đơn giản, không cần update tăng dần | tốn O(n) build; nếu entity ít di chuyển thì phí |

## 6. ⭐ Class Design

```cpp
struct Handle { std::uint32_t index; std::uint32_t generation; };

class ParticleStore {
public:
    Handle  spawn(Vec3 pos, Vec3 vel, const Cold& cold);  // O(1)
    void    despawn(Handle h);                             // O(1) swap-and-pop
    bool    alive(Handle h) const noexcept;                // generation check
    void    updatePhysics(float dt);                       // O(n), parallel
    template <class F> void queryRadius(Vec3 p, float r, F&& fn) const;

private:
    // WARM-SoA (pos — mọi hệ đọc) + HOT-SoA (vel — chỉ physics)
    std::vector<float> x_, y_, z_;
    std::vector<float> vx_, vy_, vz_;
    std::vector<Cold>  cold_;              // COLD-AoS
    std::vector<std::uint32_t> generation_;   // song song với dense arrays
    std::vector<Handle>        dense_to_handle_;

    // sparse-set: handle.index → vị trí trong dense arrays (hoặc INVALID)
    std::vector<std::uint32_t> sparse_;

    UniformGrid grid_;                     // spatial index, rebuild mỗi frame
};
```

**⭐ Despawn = swap-and-pop giữ SoA dày đặc:**
```cpp
void despawn(Handle h) {
    std::uint32_t d = sparse_[h.index];        // chỗ trong dense
    std::uint32_t last = size() - 1;
    // ghi đè slot d bằng phần tử cuối — trên MỌI mảng SoA song song
    x_[d]=x_[last]; y_[d]=y_[last]; z_[d]=z_[last];
    vx_[d]=vx_[last]; /* ... */ cold_[d]=std::move(cold_[last]);
    // sửa sparse cho phần tử vừa bị dời
    Handle moved = dense_to_handle_[last];
    sparse_[moved.index] = d; dense_to_handle_[d] = moved;
    // pop + bump generation của slot vừa chết (vô hiệu handle cũ)
    ++generation_[h.index];
    pop_back_all();
}
```
Không "chừa lỗ" trong SoA → iterate physics vẫn tuyến tính, cache-hot.

**⭐ Generation chống stale handle (ABA):** despawn tăng `generation_[index]`. `alive(h)` so
`h.generation == generation_[h.index]`. Slot tái dùng cho entity mới → generation lệch → handle
cũ tự vô hiệu. Đây chính là câu ABA của W30/W39.

## 7. ⭐ Concurrency

- **Physics update:** chia `[0,n)` thành N range liền, thread `t` xử lý range của nó. Đọc/ghi
  `x_,vx_...` trong range riêng → **không** cần lock, **không** true sharing.
- **Cạm bẫy W31 — false sharing ở BIÊN range:** nếu range chia không theo bội cache-line, phần
  tử cuối range `t` và đầu range `t+1` chung một line → ping-pong. **Sửa:** căn biên range theo
  bội 16 float (64 B), hoặc `#pragma omp parallel for` với chunk lớn.
- **Per-thread counters/scratch** (đếm va chạm) phải `alignas(64)` (W31 — 27×), rồi reduce cuối.
- **Spawn/despawn giữa frame:** không cho chạy song song với physics (đổi n). Gom thành
  **command buffer**, apply **giữa các frame** (single-thread) → tránh race hoàn toàn.

## 8. ⭐ Memory

- **Entity storage:** `reserve(max_entities)` một lần lúc init → physics không bao giờ realloc
  giữa frame (realloc = cấp phát HĐH = jitter). SoA vector không đổi capacity trong frame.
- **Spawn pool:** slot ID tái dùng qua free-list (W30) — nhưng ở đây sparse-set *đã* là pool
  của chỉ số; "pool" chính là mảng dense + free generation. Không cần allocator riêng.
- **Buffer tạm mỗi frame** (cặp va chạm, danh sách query): **arena** (W29) — bump-allocate
  trong frame, `reset()` một phát cuối frame. Trivially-destructible → hợp arena.
- **Không pad tràn lan:** chỉ `alignas(64)` cái *ghi nóng đa luồng*. Pad 10M entity = phí RAM
  khổng lồ (W31 §8) — chỉ pad per-thread accumulator (số ít).

## 9. Failure Handling

- **Vượt max_entities:** spawn trả `std::optional<Handle>`/`expected` (W17), không throw giữa
  frame nóng; hoặc grow pool theo chunk *giữa* frame (không giữa physics).
- **Stale handle:** `alive()` false → thao tác no-op, không UB (generation cứu).
- **Query rỗng/điểm ngoài grid:** trả 0 kết quả, không lỗi.

## 10. Scaling

- **Vertical:** SoA + SIMD + N core gần tuyến tính (nếu tránh false sharing). 10M entity physics
  ~vài ms trên 8 core.
- **Grid → hierarchical:** entity phân bố lệch → grid đều kém; đổi **loose grid / BVH / octree**.
- **Vượt 1 máy:** spatial partition (chia không gian cho nhiều máy), entity gần biên nhân bản
  (ghost) — nhưng đề chốt in-memory 1 máy, đây là câu "nếu scale tiếp".

## 11. Production Considerations

- **Data-oriented design (DOD)** thực chiến: đây là kiến trúc **ECS** (Entity-Component-System)
  của game engine — EnTT, Unity DOTS, Bevy đều sparse-set + SoA archetype.
- **Đo bằng `perf c2c`** để bắt false sharing biên range; `perf stat` cache-miss cho AoS/SoA
  (W31 — cần `perf_event_paranoid<=1`).
- **`std::mdspan` (C++23)** có thể bọc SoA thành view đa chiều, giữ interface sạch.
- **Handle 32+32 bit:** 4 tỉ index, generation cuốn vòng 4 tỉ lần despawn/slot — đủ thực tế.

---

## Follow-up phỏng vấn

- Render cần đọc *cả* pos+màu+model của mỗi entity nó vẽ. Layout hybrid này có làm render **chậm**
  hơn AoS thuần không? Khi nào cái giá SoA cho physics *không* bù được cái mất cho render?
- Sparse-set tốn `sizeof(uint32)*max_index` cho `sparse_` dù ít entity sống. Khi nào nên đổi sang
  hash map handle→dense? (gợi ý: index thưa, max_index rất lớn)
- Generation 32-bit cuốn vòng: kịch bản nào làm ABA vẫn xảy ra? Cách giảm? (gợi ý: generation
  rộng hơn, hoặc không tái dùng slot ngay)
- Physics chia range tĩnh — nếu chi phí mỗi entity không đều (một số cần collision nặng) thì load
  imbalance. Dùng gì? (gợi ý: work-stealing, chunk nhỏ động — nối W36 ThreadPool)
- `queryRadius` trả kết quả qua callback `fn` thay vì `vector` — vì sao? (gợi ý: tránh cấp phát,
  arena, hoặc để caller quyết định lưu trữ)
- Nếu entity thêm/bớt *component* lúc chạy (không chỉ thêm/bớt entity) — sparse-set một-archetype
  còn đủ? (gợi ý: archetype-based ECS, chuyển entity giữa các SoA-block)
