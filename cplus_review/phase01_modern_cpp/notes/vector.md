# W5 — Build `Vector` · đỉnh của Phase 1

> Mục **5** để trống — tự viết. Đây là chỗ chuỗi W1→W5 khép lại.

---

## 0. Bằng chứng (tự đo — `/tmp/vec_proof.cpp`)

```
push 1000 phan tu  -> reallocs = 11        ← ~log2(1000), KHÔNG phải 1000
Nx (move noexcept) -> moves=3 copies=0     ← realloc MOVE
Tw (move co the nem)-> moves=2 copies=1     ← realloc COPY
```

5/5 test xanh, **kể cả dưới ASAN** — placement new + `~T()` gọi tay + try/catch
không leak, không double-free.

---

## 1. capacity ≠ size · geometric growth

`vector` giữ **hai** con số: `size_` (đang có) và `cap_` (chứa được trước khi realloc).

`push_back` khi đầy → nhân đôi capacity (`cap_ * 2`), không phải +1.
- +1 mỗi lần → n phần tử tốn **O(n²)** realloc
- nhân đôi → ~log₂(n) realloc → **amortized O(1)** mỗi push

Bằng chứng: 1000 push = **11** realloc (1→2→4→…→1024).

---

## 2. placement new — tách CẤP PHÁT khỏi KHỞI TẠO

| | cấp phát bộ nhớ | gọi ctor/dtor |
|---|---|---|
| `new T()` | ✅ | ✅ |
| `::operator new` / `::operator delete` | ✅ | ❌ |
| placement new `::new(p) T()` / `p->~T()` | ❌ | ✅ |

`vector` xin sẵn bộ nhớ cho N phần tử **nhưng chưa construct cái nào**
(`size=0, cap=N`). Nên nó phải:
- construct từng phần tử bằng **placement new**: `::new (&data_[i]) T(...)`
- huỷ từng phần tử bằng **destructor gọi tay**: `data_[i].~T()`
- rồi mới `::operator delete` bộ nhớ thô

> **Quy tắc:** cái gì construct bằng placement new thì phải huỷ bằng `~T()` gọi tay.
> `operator delete` KHÔNG gọi destructor hộ.

Thứ tự bắt buộc: **`~T()` từng phần tử TRƯỚC → `::operator delete` SAU.**
Đảo lại = gọi destructor trên vùng nhớ đã free = use-after-free.

Mô-típ này lặp ở 3 chỗ: `reserve`, `clear`, `~Vector`.

---

## 3. `move_if_noexcept` — trái tim, thứ W1→W3 dẫn tới

```cpp
::new (&new_data[i]) T(std::move_if_noexcept(data_[i]));
```

Một dòng, hai hành vi, quyết định lúc biên dịch:
- `T` có move ctor **`noexcept`** → trả `T&&` → **MOVE** (nhanh & an toàn)
- move của `T` **có thể ném** → trả `const T&` → **COPY** (chậm, nhưng giữ strong guarantee)

Vì sao: nếu move ném **giữa lúc** realloc, buffer cũ đã bị rút ruột dở dang →
mất dữ liệu. Copy thì nguồn còn nguyên → vẫn lùi được.

**Đo được:** `Nx` (noexcept) → realloc copies=0. `Tw` (throwing) → realloc copies=1.
Cùng vector, cùng thao tác, **khác đúng một keyword.**

---

## 4. Strong guarantee viết tay (try/catch trong `reserve`)

```cpp
std::size_t i = 0;
try {
    for (; i < size_; ++i)
        ::new (&new_data[i]) T(std::move_if_noexcept(data_[i]));
} catch (...) {
    for (std::size_t j = 0; j < i; ++j) new_data[j].~T();   // dọn phần dở
    ::operator delete(new_data);
    throw;                                                   // buffer CŨ chưa đụng
}
```

Test `BombOnCopy`: ném ở phần tử thứ 2 khi realloc → `live` không đổi (không leak)
**và** `v[0]==1, v[1]==2` (buffer cũ nguyên vẹn). Cùng ý tưởng copy-and-swap (W4):
không phá cái đang có cho tới khi cái mới chắc chắn xong.

---

## 5. Chuỗi W1→W5 khép lại — **TỰ VIẾT**

Viết 3–4 câu nối 5 tuần thành một mạch. Gợi ý các mốc:
- W1: đo được gì về `noexcept` và `std::vector`?
- W3: *vì sao* vector cư xử như vậy? (guarantee gì?)
- W4: copy-and-swap đạt guarantee đó thế nào?
- W5: dòng code nào ra quyết định move-vs-copy? Bạn tự viết nó ở đâu?

**Trả lời:**




---

## 6. Câu hỏi phỏng vấn tiếp theo

- `push_back` có iterator invalidation không? Khi nào một `T& r = v[0];` trở thành
  dangling? (→ W12 đào sâu)
- Vì sao `emplace_back` có thể nhanh hơn `push_back`? (gợi ý: construct tại chỗ vs
  tạo temporary rồi move — dùng `Probe` để đo ở W6)
- `std::vector` growth factor thực tế: libstdc++ dùng 2, MSVC dùng 1.5. Vì sao 1.5 có
  thể *tốt hơn* cho việc tái dùng bộ nhớ? (gợi ý: 2ⁿ không bao giờ vừa lại các block cũ)
- `vector<bool>` — vì sao nó là một lời nói dối? (không chứa `bool`, không phải container
  đúng nghĩa)
