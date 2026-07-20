# W13 — `list` / `forward_list` · Khi Big-O thua phần cứng

> Mục **6** để trống — tự viết.

---

## 0. Bằng chứng (tự đo — `bench/bench_list_vs_vector.cpp`, release-23)

```
BM_TraverseVector           5,232 ns
BM_TraverseList            22,619 ns      ← list CHẬM GẤP 4.3 LẦN

BM_FindAndInsertVector    254,878 ns
BM_FindAndInsertList    1,804,989 ns      ← list CHẬM GẤP 7 LẦN   ⚠️ ngược sách giáo khoa

BM_InsertAtKnownPosVector     823 ns
BM_InsertAtKnownPosList      14.9 ns      ← list NHANH GẤP 55 LẦN ✅
```

---

## 1. Lý thuyết sách giáo khoa (và vì sao nó lừa)

| Thao tác | vector | list |
|---|---|---|
| Chèn/xoá giữa | O(n) — dời phần tử | **O(1)** — đổi con trỏ |
| Random access | **O(1)** | O(n) |

Kết luận "hiển nhiên": cần chèn/xoá giữa nhiều → dùng `list`.
**Đo thật: SAI, vector thắng gấp 7 lần.**

## 2. Vì sao sai — thứ Big-O không đếm

> **Big-O đếm SỐ thao tác. Nó không đếm GIÁ của mỗi thao tác.**

```
Đọc từ L1 cache:   ~1 ns
Đọc từ RAM:      ~100 ns     ← gấp 100 lần
```

**vector — liền kề:**
```
[a][b][c][d][e][f][g][h]
└──── 1 cache line (64B) ~16 int ────┘
```
Đọc `a` → CPU kéo cả cache line → `b,c,d...` **đã nằm sẵn trong cache**. CPU còn
**prefetch** đoán trước. Duyệt gần như miễn phí.

**list — node rải rác:**
```
[a]──►[b]──►[c]──►[d]    mỗi node ở địa chỉ ngẫu nhiên trên heap
 ↑miss ↑miss ↑miss ↑miss
```
Mỗi node = một **cache miss** (~100ns). Không prefetch được vì không đoán được node kế
ở đâu. 10.000 phần tử = 10.000 cache miss.

**Và:** `vector::insert` dời đuôi bằng **`memmove`** — phần cứng tối ưu cực mạnh (SIMD,
chép cả cache line). "O(n)" của vector rẻ hơn "O(1)" của list rất nhiều.

## 3. Cái bẫy "O(1)" của list

> **"list chèn O(1)" chỉ đúng khi bạn ĐÃ CÓ iterator tới chỗ đó.**

Để có iterator phải **tìm** — mà tìm trong list là O(n) **đi bộ với n cache miss**.
Chi phí tìm nuốt chửng lợi thế chèn. Đó là cặp benchmark #2.

Cặp #3 **cho không** list phần tìm (lấy iterator ngoài vòng đo) → list thắng **55 lần**.
Đó mới là sở trường thật: phẫu thuật con trỏ thuần tuý.

## 4. Vậy khi nào dùng list

> Dùng `list` khi **đã giữ sẵn iterator** tới chỗ cần thao tác — không phải tìm.

- **LRU cache** — giữ iterator trong hash map, không bao giờ phải tìm (→ W55)
- Iterator/con trỏ **không bao giờ invalidate** (W12: list chỉ giết node bị xoá)
- Phần tử **rất lớn** (dời tốn kém)
- **`splice`** — nối 2 list trong O(1), vector không làm được

Còn nếu phải *tìm rồi mới thao tác* → **vector gần như luôn thắng**.

## 5. Bài học phụ: benchmark này từng SEGFAULT

Bản đầu dùng `pop_back()` để giữ size ổn định:
```cpp
auto mid = std::next(l.begin(), kSize/2);   // lấy 1 lần
for (auto _ : state) {
    l.insert(mid, 42);    // chèn TRƯỚC mid → số phần tử SAU mid không đổi
    l.pop_back();         // xoá ở CUỐI    → số phần tử sau mid GIẢM 1
}
```
`mid` **trôi dần** về cuối; tới vòng ~10.000 nó **chính là** phần tử cuối → `pop_back`
xoá đúng node `mid` trỏ tới → `insert(mid)` = **use-after-free** → segfault.

Đúng nguyên tắc vàng W12: *`erase` giết iterator trỏ tới phần tử bị xoá.*

**Sửa:** xoá đúng cái vừa chèn (`auto it = l.insert(mid,42); l.erase(it);`) → size và
vị trí ổn định vĩnh viễn.

> Người viết bài học W12 vẫn dính bug W12 ngay tuần sau. Đó là lý do ta **đo**, không tin trí nhớ.

## 6. Vì sao "cache locality thắng Big-O" — **TỰ VIẾT**

Gợi ý: dùng con số ở mục 0 giải thích. Vì sao vector làm *nhiều việc hơn* mà vẫn nhanh
hơn? Khi nào thì Big-O **vẫn** là thước đo đúng (gợi ý: n lớn tới mức nào?)

**Trả lời:**




---

## 7. Câu hỏi phỏng vấn tiếp theo

- `forward_list` khác `list` gì? Vì sao nó không có `size()`? *(O(1) memory, phải đếm)*
- `std::list::splice` — vì sao O(1)? vector có tương đương không?
- Với n rất nhỏ (10 phần tử), list vs vector khác nhau nhiều không? Vì sao?
- Khi nào cache locality **không** cứu được vector? *(phần tử cực lớn → mỗi phần tử đã
  là một cache line riêng)*
- `std::deque` nằm ở đâu trong bức tranh này? (khối liền kề nhỏ → cache tốt hơn list,
  kém hơn vector)
