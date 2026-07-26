# W28 — Placement `new` · Manual Lifetime · `std::launder`

> Artifact: [`include/w28/storage.hpp`](../include/w28/storage.hpp) · Test: [`tests/storage_test.cpp`](../tests/storage_test.cpp)
> Mục **7** để trống — tự viết.

---

## 0. Bằng chứng

```
ALL GREEN — debug/asan/ubsan × C++20 và C++23.
Test "orphan" (placement new, cố ý QUÊN ~T()):  ASAN IM LẶNG.
→ quên destructor KHÔNG phải heap leak — sanitizer không bắt. Đó là loại bug khó nhất.
```

---

## 1. ⭐ Ý tưởng trung tâm: tách *storage* khỏi *object*

`new`/`delete` thường **gộp** hai việc: cấp phát bộ nhớ *và* construct object. Placement new **tách** chúng ra:

| Trách nhiệm | Ai lo | Trong `Storage<T>` |
|---|---|---|
| **storage** (byte buffer, đúng size+align) | chủ của buffer | `alignas(T) std::byte buf_[sizeof(T)]` — là member |
| **object** (T sống trong buffer) | `construct()` / `destroy()` | placement new ↔ `~T()` tường minh |

Đây đúng là kiến trúc bên trong `std::optional` và **mọi container STL**: buffer cấp một lần, object construct/destroy từng cái theo nhu cầu.

---

## 2. Bốn mảnh

```cpp
alignas(T) std::byte buf_[sizeof(T)];         // (a) storage thô, chưa có object
T* p = ::new (buf_) T(args...);               // (b) construct TẠI chỗ, không cấp phát
p->~T();                                       // (c) hủy object, KHÔNG đụng storage
*std::launder(reinterpret_cast<T*>(buf_))      // (d) đọc object qua con trỏ đã "giặt"
```

- **(a)** `alignas(T)` bắt buộc — chỉ đủ `sizeof` mà lệch align → placement new là **UB**.
- **(b)** `::new (ptr) T` — global placement new, không cấp phát, trả con trỏ tới object mới trong `ptr`.
- **(c)** destructor tường minh — xem §3.
- **(d)** `std::launder` — xem §4.

---

## 3. ⭐ Hủy thủ công là HAI bước tách rời (quiz Q2 — tôi chọn `operator delete`, sai)

```cpp
p->~T();          // bước "object": chạy destructor, KHÔNG trả bộ nhớ
// buf_ tự thu bởi CHỦ của nó (scope kết thúc / free() / arena reset)
```

`operator delete(buf)` **sai hai lần**:
1. Nó **không chạy `~T()`** → object bị bỏ rơi chưa hủy → tài nguyên bên trong T (string, handle…) **leak**.
2. Nó **giả định** buf đến từ `operator new`. Placement new dùng được trên *mọi* storage (stack, member, arena) → gọi `operator delete` trên stack buffer = **UB**.

**Quy tắc:** placement new không cấp storage → `~T()` thủ công không giải phóng storage. Ai cấp thì người đó thu.

**Hai bẫy:**
- **Double destroy:** gọi `~T()` rồi để scope hủy lần nữa (nếu buf là object thật) = UB → nên buf phải là *raw bytes*.
- **Quên `~T()`:** object leak *âm thầm*. Test `RawPlacementNewWithoutDestroySkipsTheDestructor` chứng minh: `dtor == 0`, và **ASAN không kêu** (buffer stack vẫn thu) — chỉ tài nguyên *trong* T mới rò. RAII của `Storage` (`~Storage` gọi `destroy()`) chính là liều thuốc.

---

## 4. `std::launder` — vá con trỏ sau khi tái construct (quiz Q4 — đúng)

Tình huống: construct T mới **đè lên** chỗ T cũ vừa hủy, mà T có **const member** hoặc **reference member**:
```cpp
struct T { const int id; };
new (buf) T{1};
p->~T();
T* q = new (buf) T{2};          // object MỚI, id khác
// đọc qua con trỏ CŨ p->id  → compiler được phép cache "id==1" (const không đổi) → UB
int v = std::launder(p)->id;    // launder: "p trỏ object hiện đang sống ở đây, đọc lại"
```
Compiler giả định const/reference member bất biến qua vòng đời một object → sau khi tái construct, con trỏ cũ có thể trả giá trị cũ đã cache. `std::launder(p)` cắt giả định đó. Trong `Storage::get()` ta luôn `launder` cho an toàn khi tái dùng storage (test `ReconstructInPlace`).

> Overview thôi — không cần thuộc từng ngóc ngách chuẩn; chỉ cần: "reinterpret_cast + tái construct trên storage cũ + const/ref member ⇒ nghĩ tới launder".

---

## 5. Vì sao `std::vector` phải placement new, không `new T[]` (quiz Q5 — đúng)

`new T[capacity]` **construct luôn cả `capacity` phần tử** ngay lập tức → đòi `T` default-constructible + tốn ctor cho ô chưa dùng. Vector cần:
- cấp **storage** cho `capacity` (một lần, qua allocator),
- **construct từng phần tử** khi `push_back` (placement new tại `data()+size`),
- **destroy từng phần tử** khi `pop_back`/`erase` (`~T()` tường minh), giữ nguyên capacity.

`pop_back` = `~T()` + giảm size (giữ buffer); `~vector` = hủy hết rồi trả buffer. Đúng cặp construct/destroy của tuần này, quy mô mảng.

---

## 6. `std::aligned_storage` đã chết (C++23)

`std::aligned_storage<Size, Align>` bị **deprecated C++23** (khó dùng đúng, dễ sai launder). Cách hiện đại: `alignas(T) std::byte buf[sizeof(T)]` — rõ ràng, đúng align, không cần trait.

---

## 7. TỰ VIẾT (retrieval — tự điền, không nhìn phía trên)

> Đề: **Bạn placement-new một object có `std::string` member vào một `std::byte` buffer trên stack, rồi để scope kết thúc mà KHÔNG gọi `~T()`. ASAN có báo lỗi không? Cái gì rò, cái gì không? Vì sao? Và `Storage<T>` sửa điều này bằng cơ chế gì?** 4–5 câu.

_(câu trả lời của bạn ở đây)_

---

## 8. Câu hỏi phỏng vấn nối tiếp

- `std::construct_at` / `std::destroy_at` (C++20) thay `new (p) T` / `p->~T()` chỗ nào, và lợi gì (gợi ý: constexpr)?
- Nếu ctor của T **ném** giữa chừng khi placement new, ai chịu trách nhiệm dọn? Object đã "sống" chưa?
- Vì sao `std::optional` không chỉ giữ `T` + `bool` mà phải dùng union/aligned storage? (gợi ý: T không default-constructible)
- `std::vector<bool>` phá vỡ mô hình placement-new-per-element thế nào?
- Khi nào `std::launder` là **bắt buộc** vs chỉ "phòng xa thừa"? Nêu một trường hợp bỏ nó vẫn đúng.
- Placement new mảng `new (buf) T[n]` có cạm bẫy gì khiến gần như không ai dùng? (gợi ý: overhead cookie không xác định)
