# W37 — Atomics + `memory_order` (relaxed / release-acquire / seq_cst)

> Artifact: [`include/memory_order_litmus.hpp`](../include/memory_order_litmus.hpp) · [`tests/memory_order_test.cpp`](../tests/memory_order_test.cpp)
> Tuần **học ngôn ngữ**, không clone component: `memory_order` là quy tắc của mô hình bộ nhớ, không
> có gì để "xây lại". Artifact = 4 demo gói thành hàm test được + Store-Buffer litmus **đếm** kết cục.
> Mục **6** để trống — tự viết.

---

## 0. Bằng chứng (full matrix xanh, số đo thật)

```
SeqCstForbidsStoreBufferReorder      : 20000 vòng seq_cst → (0,0) xuất hiện 0 lần. GUARANTEE.   ✓
RelaxedCanReorder_Observed           : 20000 vòng relaxed → in số (0,0) quan sát được.
                                       Máy x86 này: 0/20000 lần này — KHÔNG assert (tránh flaky). ✓
ReleaseAcquirePublishesPayload       : release-store publish data=42; acquire-load thấy 42.       ✓
WaitNotifyKeepsTheSameSynchronization: consumer PARK bằng wait(false); dậy thấy data=42.          ✓
AtomicRefSumIsExact                  : 8 thread × 100k fetch_add lên ô riêng của vector THƯỜNG
                                       → tổng == 800000, không mất update.                        ✓

Ma trận: debug-20 · debug-23 · asan-23 · ubsan-23 · tsan-20 · tsan-23  → 6/6 PASS.
TSan im lặng qua CẢ 5 demo — vì mọi demo race-free BY CONSTRUCTION (đó là cả điểm của tuần này).
```

> ⭐ Điểm chốt của "bằng chứng": TSan **không** bắt được gì, và đó mới đúng. `memory_order` không phải
> để tránh data race (atomic đã lo phần đó) — nó quyết định **THỨ TỰ NHÌN THẤY** quanh atomic đó.
> Chỉ assertion chấm điểm là `seq_cst ⇒ 0` lần `(0,0)`. Con số relaxed là để **thấy**, không để assert:
> khẳng định "relaxed > 0" là test flaky (một lần chạy hợp lệ có thể ra 0; nền strongly-ordered không
> bao giờ reorder qua barrier).

---

## 1. ⭐ Ba tầng đảm bảo — `atomic` lo 1 biến, `memory_order` lo THỨ TỰ quanh nó

Hai câu hỏi **tách rời** mà người mới hay gộp:

1. **"Thao tác trên biến X có bị xé nửa / mất update không?"** → trả lời bằng **`std::atomic<X>`**.
   `fetch_add` là read-modify-write **không thể chia cắt**. Đây là *atomicity*.
2. **"Các ghi/đọc biến KHÁC quanh X, thread kia thấy theo thứ tự nào?"** → trả lời bằng **`memory_order`**.
   Đây là *visible ordering* (happens-before). `data` trong demo là `int` **thường** và vẫn an toàn — vì
   cờ release/acquire tạo cạnh happens-before, không phải vì `data` là atomic.

Ba mức, từ lỏng đến chặt:

| `memory_order` | Đảm bảo | Giá | Dùng khi |
|---|---|---|---|
| `relaxed` | chỉ atomicity; **không** ràng buộc thứ tự với thao tác khác | rẻ nhất | đếm thuần (counter/ref-count tăng), không ai đọc dữ liệu khác *qua* cờ này |
| `release` (store) / `acquire` (load) | store publish MỌI ghi TRƯỚC nó; load nào đọc được giá trị đó thấy hết | trung bình | **message passing**: 1 cờ mở khóa 1 vùng dữ liệu (producer→consumer) |
| `seq_cst` (mặc định) | như release/acquire **+ một total order toàn cục** trên mọi thao tác seq_cst | đắt nhất | khi cần lý luận "tồn tại 1 thứ tự chung mà mọi thread đồng ý" — vd litmus Store-Buffer |

---

## 2. ⭐ Store-Buffer litmus — vì sao seq_cst đắt hơn release/acquire

Release/acquire **không đủ** cho một số bài. Litmus kinh điển:

```
khởi tạo x=0, y=0
Thread 0:  x.store(1);  r1 = y.load();
Thread 1:  y.store(1);  r2 = x.load();
Hỏi: có thể r1==0 && r2==0 KHÔNG?
```

- Với **relaxed/release-acquire**: **CÓ**. Đây chính là **StoreLoad** — reorder duy nhất mà **x86 thực sự
  làm** (store vào store-buffer, load đọc bộ nhớ *trước khi* store kia kịp drain). Mỗi thread thấy store
  của mình rồi mới thấy store kia; hai "phim" không khớp thành một dòng thời gian chung.
- Với **seq_cst**: **KHÔNG**. seq_cst ép **một total order duy nhất** trên tất cả thao tác seq_cst. Trong
  order đó, một trong hai store phải đứng trước load của thread kia ⇒ ít nhất một `r` phải là 1.

Đó là thứ release/acquire **không** cho: release/acquire chỉ đồng bộ **theo cặp** (store nào ↔ load nào
đọc được nó), **không** dựng một dòng thời gian toàn cục. `seq_cst` mua chính xác cái total order đó — và
trả bằng một `mfence` (x86) chặn StoreLoad. Litmus test là cách *chứng minh* sự khác biệt, không chỉ đọc.

> Vì sao demo relaxed máy này ra `0/20000`? Litmus của tôi đồng bộ hai worker bằng `std::barrier` mỗi
> vòng để cửa sổ trùng nhau — nhưng barrier release hơi lệch pha nên store thường đã drain trước khi
> load kia chạy. Muốn *bắt* reorder ổn định cần vòng spin chặt + delay ngẫu nhiên (kiểu Preshing) để
> desync timing. Điểm dạy **không đổi**: seq_cst **cấm** `(0,0)` (assert cứng, luôn 0); relaxed **cho
> phép** (in ra, có thể 0). "Cho phép" ≠ "luôn xảy ra".

---

## 3. ⭐ release/acquire = message passing (bẫy thứ tự)

```cpp
// Producer                             // Consumer
data = 42;                              while(!ready.load(acquire)) {}
ready.store(true, release);            assert(data == 42);   // luôn đúng
```

- **release-store** = "đóng gói" mọi ghi *đứng trước nó* (kể cả `data` thường) rồi mới bật cờ.
- **acquire-load** nào **đọc được** giá trị release đó → thấy trọn gói ghi ấy.
- ⭐ **Bẫy thứ tự**: `data = 42` PHẢI đứng **trước** `store(release)`. release chỉ gói cái gì *trước* nó;
  đặt `data = 42` *sau* store là vứt bảo đảm. Tương tự, đọc `data` phải *sau* acquire-load thành công.

`wait/notify` (§4) là **đúng cơ chế đồng bộ này** — chỉ khác cách chờ.

---

## 4. ⭐ `atomic::wait/notify` vs busy-wait — và vs `condition_variable`

`while(!ready.load(acquire)) {}` **đốt 1 core 100%** để hỏi đi hỏi lại. Thay bằng:

```cpp
// Producer: data=42; ready.store(true,release); ready.notify_one();
ready.wait(false, acquire);   // CÒN false → PARK (OS ru ngủ, 0% CPU); chỉ dậy khi bị notify
```

- Tham số đầu của `wait(v)` = giá trị bạn **KHÔNG** muốn: "ngủ *trong khi* nó == `v`". Dậy → re-check.
- **Quan trọng**: `wait` vẫn là **acquire-load**, `store` phía trước vẫn là **release-store**. Cơ chế
  *synchronizes-with* **y hệt** release/acquire. wait/notify đổi **CÁCH chờ** (park thay vì spin), **không**
  đổi **CÁCH đồng bộ**. `data` vẫn không cần atomic.
- **vs `condition_variable` (W33)**: cùng ý "ngủ tới khi có tín hiệu", nhưng `cv` cần **mutex + predicate**.
  `atomic::wait` là bản **không mutex**, gọn cho một-cờ-đơn. **Không** thay được `cv` khi phải bảo vệ một
  **vùng dữ liệu phức tạp** (nhiều biến, cần loại trừ tương hỗ) — đó vẫn là đất của `cv`.

---

## 5. ⭐ `atomic_ref` — mượn atomic cho biến THƯỜNG (và cái bẫy tôi gài)

Không dựng được `vector<atomic<int>>` (atomic **không copyable** → vector không cấp phát/resize được).
`atomic_ref<int>` là **khung nhìn atomic tạm** phủ lên một `int` **đã tồn tại**:

```cpp
vector<int> counters(8, 0);                  // int THƯỜNG
atomic_ref<int> slot{counters[i]};           // khung nhìn
slot.fetch_add(1, relaxed);                  // giờ thao tác này atomic
// slot chết → counters[i] lại là int thường
```

- **Luật sống-chết**: chừng nào còn một `atomic_ref` trỏ vào biến, **MỌI** truy cập biến đó phải qua ref
  (trộn truy cập thường đồng thời = data race). Hết scope ref → biến là plain `int` lại.
- **atomic ở TYPE vs ở REFERENCE**: `atomic<int>` sở hữu atomicity **vĩnh viễn ở mức kiểu**;
  `atomic_ref<int>` mượn atomicity **chỉ khi ref còn sống, ở mức tham chiếu**.
- Cần biến đích thỏa `atomic_ref<T>::required_alignment`.

> ⭐ **Bẫy đã gài & đáp án**: "8 thread mỗi thread chạm ô *riêng* → cần `atomic_ref` làm gì?" — Không cần!
> Các **phần tử khác nhau của mảng là các memory location riêng biệt** theo mô hình bộ nhớ C++, nên
> `counters[i]++` thường ở pattern này **không phải data race và vẫn đúng**. Demo chỉ trưng *cú pháp*.
> `atomic_ref` **thật sự** cần khi nhiều thread đập vào **CÙNG một biến** thường (vd accumulator chung).
> Và cái tôi nhắc — 8 `int` chung 1 cache line 64B → core giành line (**false sharing**, W31) — là vấn đề
> **hiệu năng, KHÔNG phải tính đúng**; `atomic_ref` không chữa nó (muốn chữa: `alignas(64)` tách line).
> ⇒ **memory_order = correctness; false sharing = performance. Đừng lẫn.**

---

## 6. TỰ VIẾT (retrieval — tự điền, không nhìn phía trên)

> Đề: **Tách hai câu hỏi mà `atomic<T>` và `memory_order` trả lời (mỗi cái một câu). Ba mức
> relaxed/release-acquire/seq_cst — mỗi mức đảm bảo gì và dùng khi nào? Store-Buffer litmus: viết ra,
> và giải thích vì sao seq_cst cấm `(0,0)` còn release/acquire thì không. `atomic::wait/notify` đổi cái gì
> so với busy-wait, và KHÔNG đổi cái gì? `atomic_ref` khác `atomic<int>` ở đâu, và khi nào nó thực sự cần
> thiết (không phải khi mỗi thread có ô riêng)?** 8–10 câu.

_(câu trả lời của bạn ở đây)_

---

## 7. Câu hỏi phỏng vấn nối tiếp

- Vì sao trên **x86** phần lớn atomic load/store *"miễn phí"* dù bạn viết `seq_cst`? (gợi ý: x86 là TSO —
  strongly ordered; chỉ **StoreLoad** được reorder. Load acquire / store release map thẳng thành `mov`;
  chỉ `seq_cst` store cần `mfence`/`xchg`. Trên ARM/POWER thì đắt hơn nhiều — đó là lý do đo trên nhiều
  kiến trúc mới thấy sự khác biệt.)
- `fetch_add(1, relaxed)` cho ref-count `shared_ptr` khi **tăng** thì được, nhưng khi **giảm** phải
  `acq_rel`. Vì sao? (gợi ý: giảm về 0 là lúc gọi dtor — cần **acquire** để thấy mọi ghi của thread khác
  trước khi hủy, và **release** để publish việc mình dùng xong. Tăng thì chỉ cần atomicity.)
- Có phải cứ dùng `seq_cst` cho chắc là không bao giờ sai? (gợi ý: **đúng về correctness**, sai về
  **performance & thông điệp**. seq_cst khắp nơi che mất ý định và thêm fence thừa; nhưng "relaxed hóa
  sớm" là nguồn bug lock-free số 1 — quy tắc: **mặc định seq_cst, hạ mức chỉ khi đo được và chứng minh
  được**.)
- `atomic::wait` có thể **spurious wakeup** không? (gợi ý: có thể dậy giả — nên nó tự re-load và so lại
  `old` bên trong; bạn **không** cần vòng `while` như cv, nhưng ngữ nghĩa "chỉ trả về khi giá trị ĐÃ khác"
  là nhờ nó tự lặp.)
- Store-Buffer `(0,0)` khó bắt trên x86 nhưng **IRIW** (independent reads of independent writes) còn cần
  gì hơn? (gợi ý: đó là litmus mà chỉ `seq_cst` mới cứu, thể hiện rõ "total order" — hai reader thấy hai
  writer theo thứ tự ngược nhau; release/acquire cho phép, seq_cst cấm.)
- **What would a Staff Engineer improve?** (gợi ý: (1) đưa litmus lên khung Preshing spin+random-delay để
  *thực sự* quan sát relaxed reorder, biến §0 relaxed từ "0/20000" thành số dương lặp lại được;
  (2) chạy matrix trên **ARM** (qemu/CI) để seq_cst-vs-acquire lộ chi phí thật; (3) `perf stat` đếm
  `mem_inst_retired` / số `mfence` giữa relaxed vs seq_cst để *định lượng* cái giá; (4) thêm một demo
  **sai** cố ý — relaxed cho message passing — để TSan/kết quả *chứng minh* nó hỏng, đóng vòng
  "correctness vs performance".)
