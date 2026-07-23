# Design Round #2 — LRU Cache O(1)

**Boundary:** cuối Phase 2 (W20)
**Đề:** thiết kế cache dung lượng cố định, `get`/`put` đều **O(1)**, khi đầy thì đuổi
phần tử **lâu nhất không được dùng** (Least Recently Used).

Đây là W13 (`list`) + W14 (hash map) ghép lại. Artifact thật sẽ xây ở **W55**.
Theo System Design Workflow của CLAUDE.md (11 bước).

---

## 1–2. Requirements & Clarifications

| Câu hỏi làm rõ | Chốt | Vì sao |
|---|---|---|
| `get` miss trả gì? | **`std::optional<Value>`** | Miss là **bình thường** (hit rate 80% → 20% miss). W17: hiếm→exception, thường→optional. Throw tốn ~µs, optional tốn ~ns |
| Cấu trúc giữ thứ tự recency? | **`std::list`** | `splice` = O(1). vector `erase` giữa = O(n). `priority_queue` không có API sửa priority + chỉ O(log n) |
| Hash map map `key → ?` | **`key → list::iterator`** | Map `key→Value` lấy được *giá trị* O(1) nhưng mất dấu *vị trí* → phải `std::find` trong list = **O(n)**, hỏng yêu cầu |

## 3. Constraints
- `get`/`put` **O(1) amortized** — hash O(1) trung bình + splice O(1) thật
- Bộ nhớ ~**2x**: mỗi entry = 1 node list (2 con trỏ + Entry) + 1 slot hash map
- Không thread-safe (Phase 5)
- `capacity` cố định lúc khởi tạo

## 4. Architecture

```
          map_ (hash — O(1) tra cứu)
   key ──────────────────────────► iterator
                                      │
                                      ▼
   order_ (list — O(1) đổi thứ tự)
   ┌──────┐   ┌──────┐   ┌──────┐   ┌──────┐
   │ MRU  │⇄ │      │⇄ │      │⇄ │ LRU  │
   └──────┘   └──────┘   └──────┘   └──────┘
     đầu                              cuối ← nạn nhân bị đuổi
```

`hash map` giỏi *tra cứu*, dở *thứ tự*. `list` giỏi *thứ tự*, dở *tra cứu*.
Ghép lại → cả hai O(1).

> **Không cấu trúc nào giỏi mọi thứ — hãy ghép.**

## 6. Class Design

```cpp
template <typename Key, typename Value>
class LruCache {
    struct Entry { Key key; Value value; };
    using List = std::list<Entry>;

public:
    explicit LruCache(std::size_t capacity) : capacity_(capacity) {}

    [[nodiscard]] std::optional<Value> get(const Key& k) {
        auto it = map_.find(k);
        if (it == map_.end()) return std::nullopt;              // miss là BÌNH THƯỜNG
        order_.splice(order_.begin(), order_, it->second);       // O(1) — lên MRU
        return it->second->value;
    }

    void put(const Key& k, Value v) {
        if (auto it = map_.find(k); it != map_.end()) {          // ① đã có
            it->second->value = std::move(v);
            order_.splice(order_.begin(), order_, it->second);
            return;
        }
        if (map_.size() == capacity_) {                          // ③ đầy → đuổi LRU
            const Key victim = order_.back().key;                // COPY, không phải &
            order_.pop_back();
            map_.erase(victim);
        }
        order_.push_front(Entry{k, std::move(v)});               // ② thêm mới
        map_.emplace(k, order_.begin());
    }

private:
    std::size_t capacity_;
    List order_;                                     // đầu = MRU, cuối = LRU
    std::unordered_map<Key, typename List::iterator> map_;
};
```

**Rule of Zero** — không viết dtor/copy/move nào. `list` và `unordered_map` tự quản.
W3: nếu mọi thành viên tự quản, class không cần quản gì.

Chi tiết đáng nói lúc phỏng vấn:
- `if (auto it = ...; cond)` — init-statement C++17, giới hạn scope
- `Value v` by-value + `std::move(v)` — không copy thừa (W4)
- `[[nodiscard]]` — bỏ qua kết quả một lần tra cứu là bug (W9)

## ⭐ Bẫy #1 — thứ tự xoá nạn nhân

```cpp
// ❌ SAI                                  // ✅ ĐÚNG
order_.pop_back();                         const Key victim = order_.back().key;
map_.erase(order_.back().key);             order_.pop_back();
//         ^^^^^^^^^^^^^ node ĐÃ CHẾT      map_.erase(victim);
```
`pop_back()` gọi `~Entry` + `delete` node. Đọc `order_.back()` sau đó = **use-after-free**.
Phải lấy key **trước** khi xoá.

## ⭐ Bẫy #2 — `const Key&` là dangling reference

```cpp
const Key& victim = order_.back().key;   // ① REFERENCE vào trong node
order_.pop_back();                       // ② node bị delete
map_.erase(victim);                      // ③ 💥 đọc rác
```
Đúng thứ tự rồi **vẫn hỏng**. `const Key&` **không sở hữu gì** — nó chỉ là *cái nhìn* vào
bộ nhớ node. Node chết → cái nhìn trỏ vào rác.

**Sửa: bỏ dấu `&`** → `const Key victim = ...` (copy).

Cùng một bug, ba lần đội lốt khác nhau:

| Tuần | Hình dạng |
|---|---|
| W13 | `mid` iterator trỏ node bị `pop_back()` xoá → segfault |
| W18 | `StringView` trỏ `std::string` tạm → string chết → dangling |
| Mock #2 Q3 | `string_view` sau `realloc` → "in ra rác" |

> **Luật:** cần giá trị sống **lâu hơn** thứ đang chứa nó → **copy**.
> Reference / view / iterator / pointer đều nói *"tôi mượn, tôi không giữ."*
> Copy một `int` hay `string` ngắn tốn vài ns. Đổi vài ns lấy việc không bao giờ
> dangling: **luôn xứng đáng**.

## `splice` — thao tác làm cả thiết kế này khả thi

```cpp
order_.splice(order_.begin(), order_, it);
//            ĐẾN đây          TỪ list này   node này
```

```
TRƯỚC:   head → [A] ⇄ [B] ⇄ [C] → null        gọi get(C)
                              ↑ it

  ① B->next = null            (gỡ C: hàng xóm nối lại)
  ② C->next = A, A->prev = C
  ③ head = C

SAU:     head → [C] ⇄ [A] ⇄ [B] → null
                 ↑ it  ← VẪN LÀ CON TRỎ CŨ, VẪN HỢP LỆ
```

Node **không nhúc nhích trong bộ nhớ** — chỉ mũi tên đổi hướng. ~6 phép gán con trỏ,
bất kể list có 3 hay 3 triệu phần tử.

**Vì sao không `erase` + `insert`?**
1. 1 `delete` + 1 `new` — cấp phát heap, đắt hơn nối con trỏ cả chục lần
2. Copy `Entry` — đắt nếu `Value` nặng
3. **💥 `it` chết** — mà `map_` đang lưu chính iterator đó → mọi entry thành dangling

Vấn đề 3 là chí mạng. Cả thiết kế dựa trên *"map lưu iterator, iterator luôn hợp lệ."*

Ba dạng splice (tất cả O(1), trừ đoạn giữa hai list khác nhau):
```cpp
l1.splice(pos, l2);               // toàn bộ l2
l1.splice(pos, l2, it);           // một node          ← ta dùng
l1.splice(pos, l2, first, last);  // một đoạn
```
`l1` được phép **trùng** `l2` — đó chính là "tự chuyển node lên đầu".

## 5. Trade-offs

| Quyết định | Được | Mất |
|---|---|---|
| `list` + `map` | O(1) cả hai chiều | ~2x bộ nhớ; node rải rác → cache miss nhiều |
| `unordered_map` | O(1) trung bình | worst-case O(n) nếu hash xấu; node-based (W14) |
| `optional` cho miss | nhanh, rõ ràng | người gọi phải kiểm — nhưng đó là điều tốt |

**Trade-off hay nhất để nói trong phỏng vấn:**
LRU cache là một trong **rất ít** trường hợp `std::list` thật sự đúng. W13 đã **đo** và
thấy `list` gần như luôn thua `vector` — vì duyệt list phá cache locality. Nhưng ở đây
**ta không bao giờ duyệt list**: ta nhảy thẳng vào node qua iterator từ hash map rồi nối
lại con trỏ. Điểm yếu duy nhất của list không bao giờ bị chạm; điểm mạnh duy nhất của nó
(splice O(1) + iterator ổn định) đúng là thứ ta cần.

> **Chọn cấu trúc theo thao tác bạn THỰC SỰ làm, không theo danh tiếng của nó.**

## 8. Memory
- Mỗi entry: `sizeof(Entry)` + 2 con trỏ (node list) + slot hash (~1.5x do load factor)
- Node list rải rác trên heap → **prefetch không giúp gì**. W20 `pmr` giải quyết đúng
  chuyện này: cấp node list từ `monotonic_buffer_resource` → node liền kề (đã đo: nhanh 2.2x)

## 9. Failure Handling
- `capacity_ == 0`: `map_.size() == 0 == capacity_` → đuổi từ list **rỗng** → UB.
  Chặn ở ctor (`throw std::invalid_argument` — đây mới là lỗi *hiếm*, đúng chỗ cho exception)
- `Value` ném lúc copy/move: nạn nhân **đã bị đuổi** nhưng phần tử mới chưa vào →
  mất dữ liệu. Đây là **basic guarantee**, không phải strong (W3)
- Hash collision nặng → `unordered_map` suy biến O(n), `get` không còn O(1)

## 10. Production
- Metrics: hit/miss ratio là **chỉ số sống còn** của cache — không đo thì không biết
  cache có ích không (bài học W20: *một benchmark tồi nguy hiểm hơn không benchmark*)
- TTL (hết hạn theo thời gian) thường cần thêm bên cạnh LRU
- Thread-safe: mutex bao ngoài là bước đầu, nhưng `get` cũng **ghi** (splice) → không
  dùng được `shared_mutex` ngây thơ. → Phase 5

## 11. Interview Follow-up
- **LRU vs LFU** — khác nhau gì? Khi nào LFU tốt hơn? (workload có "hot set" ổn định)
- Vì sao `get` lại là thao tác **ghi**? (nó splice) → hệ quả gì cho thread-safety?
- Làm sao đuổi **nhiều** phần tử một lúc hiệu quả? (`splice` cả đoạn + erase batch)
- Nếu bắt buộc dùng `vector`, làm được O(1) không? (không cho LRU thật; xấp xỉ được bằng
  CLOCK / second-chance — đây là cái OS thật sự dùng cho page replacement, → W46)
- `unordered_map<Key, list::iterator>` — lưu iterator trong container khác có an toàn
  không? Với `vector` thì sao? (không — realloc giết hết)

---

**Chốt:** Phase 2 hội tụ ở đây. Thiết kế này O(1) được **chỉ vì** một sự thật của W12:
`std::list` không bao giờ *dời chỗ* phần tử, nên iterator tới nó **luôn ổn định**.
Đó không phải chi tiết cài đặt — đó là **lý do kiến trúc**.
