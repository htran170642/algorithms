# W43 — Copy elision · RVO · NRVO

## 0. Bằng chứng

`phase06_elision_test` — **cùng 1 file** compile 2 lần, đếm bằng `Probe` counters (W1), xanh dưới cả `-20` và `-23`:

| Test | Bản thường | Bản `-fno-elide-constructors` | Chốt |
|---|---|---|---|
| `PrvalueReturnIsGuaranteedZeroMove` | **0 move** | **0 move** | ⭐ prvalue elision **BẮT BUỘC** — flag không đụng được |
| `NrvoReturnIsOptional` | 0 move | **1 move** | ⭐ NRVO **OPTIONAL** — tắt được → fallback là **move**, không copy |
| `MultipleReturnPathsMoveNeverCopy` | 1 move | 1 move | 2 object → không NRVO, nhưng vẫn implicit-move |
| `ReturningAMemberCopies` | **1 copy** | 1 copy | member không phải "movable variable" → COPY |
| `ByValueParamMovesOutNotIn` | 1 move | 1 move | prvalue dựng param tại chỗ (0 vào), `return param` = 1 ra |
| `AssignmentIntoLiveObjectCannotElide` | 1 move-assign | 1 move-assign | elision chỉ cho **khởi tạo**, không cho gán |

⭐ **Chốt đo được:** khác biệt giữa dòng 1 và dòng 2 *chính là* bài học. `-fno-elide-constructors` **lật NRVO 0→1** nhưng **không chạm** guaranteed prvalue elision — đó là bằng chứng cứng rằng hai thứ này khác nhau về mặt *bảo đảm*, không phải mức độ tối ưu.

---

## 1. Hai loại elision — KHÁC NHAU VỀ BẢO ĐẢM ⭐

| | Trả **prvalue** | Trả **biến có tên** |
|---|---|---|
| Ví dụ | `return T{...};` / `return f();` | `T x; ...; return x;` |
| Tên | **Guaranteed copy elision** (C++17) | **NRVO** (Named RVO) |
| Bảo đảm | **BẮT BUỘC** — ngôn ngữ ép | **OPTIONAL** — compiler được phép |
| Copy/move | **0** — không temporary nào tồn tại | 0 nếu NRVO; nếu không → **1 move** |
| Cần move ctor? | Không (elide cả type deleted-move) | Có — fallback dùng move ctor |

- **Vì sao prvalue bắt buộc:** C++17 định nghĩa lại prvalue = "công thức khởi tạo", **không phải object**. `T{...}` không tạo temporary rồi copy — nó *chính là* cái khởi tạo đích. Không có object thứ hai → không có gì để elide.
- **Vì sao NRVO chỉ optional:** biến có tên là object thật (có địa chỉ, `&x` được, nhiều đường `return`). Compiler phải *chứng minh* dựng thẳng vào ô return an toàn → standard chỉ **cho phép**. Bỏ cuộc thì `return x;` coi `x` như rvalue sắp chết → **implicit move**.

## 2. `return std::move(local)` là PHẢN TÁC DỤNG

```cpp
return x;              // ✅ NRVO ứng cử → 0 move
return std::move(x);   // ❌ giết NRVO → LUÔN 1 move
```
- `std::move(x)` biến id-expression thành **xvalue** → không còn "trả về tên biến" → cấm NRVO. Ép 1 move ở nơi có thể 0. `-Wpessimizing-move` bắt.
- **Ngoại lệ:** trả **member** / trả **param by-value** → NRVO vốn không áp dụng, ở đó `std::move` mới hợp lý (xem §3).

## 3. Khi elision KHÔNG cứu (bảng non-elision, đo ở §0)

| Tình huống | Kết quả | Vì sao |
|---|---|---|
| `return b ? x : y;` / nhiều return object | move | không chốt được 1 ô storage |
| `return obj.member;` | **copy** | member ≠ "movable variable" (P1825 chỉ gồm local var + param) |
| `T f(T param){ return param; }` | move | param movable nhưng **không bao giờ** NRVO |
| `existing = f();` (đã sống) | move-**assign** | elision chỉ cho *khởi tạo* |
| `throw obj;` | copy | trừ khi `throw std::move(obj)` (ngược với return!) |

## 4. `-fno-elide-constructors` — công cụ *nhìn thấy* elision

- Tắt **optional** elision (NRVO + temporary tiền-C++17). **Không** tắt guaranteed prvalue elision (C++17 luật ngôn ngữ, không phải optimization).
- Dùng để **đo**: compile 2 bản, so counter → chứng minh cái nào bắt buộc, cái nào tuỳ. Đây đúng là §0.
- ⚠️ Chỉ để dạy/đo. Production **không bao giờ** bật — nó làm chậm mọi return by-value.

## 5. Khi nào KHÔNG dựa vào / KHÔNG tối ưu

- **Đừng viết `std::move` trên return của local** để "chắc ăn" — bạn đang *giết* NRVO. Tin compiler; đọc `-Wpessimizing-move`/`-Wredundant-move`.
- **Elision là bonus, không phải hợp đồng** cho NRVO — code đúng dù nó không xảy ra (type phải move/copy được). Chỉ prvalue mới là hợp đồng.
- **Type quá nhỏ (int, pointer)** → elision vô nghĩa, move==copy. Chỉ đáng bận tâm với type có heap/resource.
- **Đo bằng counter (`Probe`), không bằng niềm tin** — `-O0 -fno-elide-constructors` để thấy cơ chế; production `-O2` để thấy thực tế. Hai con số khác nhau và cả hai đều đúng ở ngữ cảnh của nó.

---

## 6. TỰ VIẾT

<!-- Tự tay viết lại, không nhìn:
     - Hai loại elision: cái nào BẮT BUỘC, cái nào OPTIONAL? Vì sao prvalue được ép còn named thì không?
     - Vì sao return std::move(local) chậm hơn return local? -Wpessimizing-move.
     - 5 chỗ elision KHÔNG xảy ra (member, param, ternary, gán, throw).
     - -fno-elide-constructors tắt cái gì, KHÔNG tắt cái gì? Dùng để đo thế nào?
     - Fallback của NRVO là move hay copy? Vì sao không phải copy? -->

---

## 7. Câu hỏi phỏng vấn nối tiếp

1. C++17 làm gì với prvalue mà trước đó không? (định nghĩa lại prvalue = không phải object → elision thành bắt buộc, không cần move ctor accessible)
2. `return std::move(x)` với `x` là local — nhanh hơn hay chậm hơn `return x`? (chậm hơn: giết NRVO, ép 1 move)
3. Có tình huống nào `std::move` trên return **đúng**? (trả member của local, hoặc param by-value — nơi NRVO vốn không áp dụng)
4. NRVO không xảy ra thì `return x;` copy hay move? Vì sao không copy? (move — `x` sắp hết đời nên được coi như rvalue, C++11 trở đi)
5. `T a = f();` vs `a = f();` — cái nào elide? (chỉ cái đầu; cái sau là move-assign vào object đã sống, elision chỉ cho khởi tạo)
6. `-fno-elide-constructors` tắt được NRVO nhưng test prvalue vẫn 0 move — giải thích. (guaranteed elision là luật ngôn ngữ C++17, không phải optional optimization → flag không tác động)

**What would a Staff Engineer improve?**
- Đo cả `-O2` (không chỉ debug) để xác nhận elision *thực sự* xảy ra ở build production, không chỉ ở `-O0`.
- Thêm variant type **nặng** (chứa `std::vector`/buffer) để lượng hoá elision tiết kiệm *bao nhiêu byte copy*, không chỉ đếm số lần.
- Kiểm CI cả `-fno-elide-constructors` như một "regression guard": nếu ai đó vô tình thêm `std::move` phá NRVO, bản noelide và bản thường lệch counter → test đỏ.
- Ghép với `-Wpessimizing-move -Wredundant-move` bật `-Werror` (Week 0 đã có) để chặn pessimizing-move ngay lúc compile, không đợi runtime.
- Với factory trả polymorphic (`unique_ptr<Base>`), elision không áp dụng cho object bên trong — cân nhắc sink parameter / `emplace` thay vì return-by-value cho hot path.
