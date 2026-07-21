# W19 — unique_ptr / shared_ptr / weak_ptr · Đỉnh Phase 2

> Mục **6** để trống — tự viết.

---

## 0. Bằng chứng

- 6/6 test xanh, gồm cycle-leak signature và weak phá cycle.
- **ASAN bắt một bug thật** (heap-use-after-free) khi tự viết teardown — xem mục 5.

---

## 1. unique_ptr — độc quyền sở hữu

Một `T*` duy nhất. Move-only (một chủ), move dùng `std::exchange` (W4). `~` gọi `delete`.
`sizeof = 1 con trỏ` (8 byte), **0 overhead**. Chính là `UniqueResource` (Design #1) thu gọn.

## 2. shared_ptr — sở hữu chia sẻ + CONTROL BLOCK

Nhiều con trỏ cùng sở hữu một object. Object bị delete khi **chủ cuối cùng** chết →
cần **đếm số chủ**.

```
shared_ptr ─┐
shared_ptr ─┼─► [Control Block] ──► [Object]
shared_ptr ─┘     strong: 3
                  weak:   0
```

- **Control block** = cấu trúc RIÊNG trên heap, chứa `strong` + `weak`.
- Mỗi shared_ptr giữ **2 con trỏ** (object + ctrl) → `sizeof = 16` (gấp đôi unique_ptr).
- Copy shared_ptr → `++strong`, KHÔNG copy object. `useCount` = strong.

**Phân biệt then chốt:** `useCount` (số con trỏ) ≠ số object. Copy shared_ptr → useCount
tăng, số object **đứng yên** (chỉ chia sẻ, không nhân bản). Nếu copy object thật thì mới
tạo object mới.

## 3. Vì sao ref count ATOMIC

shared_ptr an toàn khi nhiều thread cùng copy/huỷ (object bên trong thì KHÔNG). Nên
strong/weak là `std::atomic`. → shared_ptr **chậm hơn** unique_ptr (mỗi copy/huỷ là 1
atomic op). Câu phỏng vấn "đừng dùng shared_ptr bừa": chi phí atomic + 16 byte.

## 4. weak_ptr phá reference cycle

```
Node A ──shared──► Node B          A giữ B, B giữ A (strong)
Node A ◄──shared── Node B          → count không bao giờ về 0 → LEAK
```
Đếm tham chiếu KHÔNG tự thoát cycle. Đo được: `Node::alive == 2` sau scope, và
LeakSanitizer báo 48 byte.

**weak_ptr quan sát, KHÔNG tăng strong** → cho một chiều dùng weak → chiều đó không giữ
object sống → count về 0 được → hết leak. Đo: đổi `prev_strong` → `prev_weak`,
`Node::alive == 0`.

`.lock()` → trả shared_ptr nếu object còn sống, else rỗng. Dùng `compare_exchange_weak`
(lock-free, W37) để tránh race giữa "kiểm strong>0" và "++strong".

## 5. ⭐ BUG ASAN BẮT: control block tự giải phóng giữa chừng

```cpp
void release() {
    if (--strong == 0) {
        delete ptr_;              // (1) ~Object có thể huỷ weak_ptr CUỐI → delete ctrl_
        if (weak == 0) delete ctrl_;   // (2) đọc ctrl_ ĐÃ CHẾT → use-after-free
    }
}
```
`delete ptr_` chạy `~Object`, mà nếu Object chứa weak_ptr cuối, `~weak_ptr` sẽ giải phóng
**chính control block**. Dòng (2) sau đó đụng vào bộ nhớ đã chết.

**Sửa (mẹo libstdc++):** phía strong giữ CHUNG một weak ref → `weak` khởi tạo = **1**.
Cái +1 giữ control block sống xuyên suốt `delete ptr_`, rồi mới nhả cuối cùng:
```cpp
ControlBlock { strong{1}; weak{1}; };   // +1 collective ref

// SharedPtr::release: strong về 0 → delete object → NHẢ weak-chung
if (--strong == 0) { delete ptr_; if (--weak == 0) delete ctrl_; }
// WeakPtr::release: chỉ cần --weak == 0 → delete ctrl_ (strong-side đảm bảo thứ tự)
```
→ control block xoá **đúng một lần, đúng lúc**. ASAN xanh.

> Debug xanh 6/6 — chỉ **ASAN** thấy bug (kích hoạt ở đúng thứ tự huỷ cycle+weak). Đây
> là lý do chạy test dưới ASAN mỗi tuần.

## 6. Vì sao control block là cấu trúc RIÊNG (không nhét vào object) — **TỰ WRITE**

Gợi ý: vì sao strong và weak phải sống độc lập với object? weak_ptr đọc gì sau khi object
chết? Nếu control block nằm trong object thì sao? `make_shared` gộp chúng thế nào và lợi
gì? (một lần cấp phát thay hai)

**Trả lời:**




---

## 7. Câu hỏi phỏng vấn — có đáp án

### 7.1 `make_shared<T>` vs `shared_ptr<T>(new T)` — số lần cấp phát

Đo được:
```
shared_ptr<Widget>(new Widget) -> 2 lần cấp phát  (object RIÊNG + control block RIÊNG)
make_shared<Widget>()          -> 1 lần cấp phát  (gộp 1 khối)
```

`shared_ptr(new T)`: `new T` cấp phát object (1), rồi constructor shared_ptr `new
ControlBlock` (2) → **2 lần**. `make_shared`: gộp object + control block vào **một khối
liền kề** → **1 lần** → nhanh hơn + cache tốt hơn (chúng cạnh nhau).

**Nhược điểm của make_shared:** object và control block chung một khối → khối chỉ được
giải phóng khi **cả** strong lẫn weak về 0. Nên một `weak_ptr` sống lâu sẽ giữ luôn **cả
vùng nhớ object** (dù object đã huỷ) — với object lớn, đây là lãng phí. `shared_ptr(new)`
thì object được free ngay khi strong=0, chỉ control block nhỏ ở lại.

> Mặc định dùng `make_shared` (nhanh hơn, an toàn exception hơn). Trừ khi object rất lớn
> và có weak_ptr sống lâu.

### 7.2 `enable_shared_from_this`

Vấn đề: bên trong một method, bạn có `this` (con trỏ thô) nhưng cần trả về một
`shared_ptr` tới chính object đó. Viết `shared_ptr<T>(this)` → **SAI**: tạo control block
THỨ HAI → hai control block cùng quản một object → double-free.

`enable_shared_from_this<T>` cho method gọi `shared_from_this()` → trả shared_ptr dùng
**đúng control block gốc**. Cơ chế: base class giữ sẵn một `weak_ptr` tới chính nó, được
set khi shared_ptr đầu tiên tạo ra object.

> Bẫy: gọi `shared_from_this()` khi object **chưa** được quản bởi shared_ptr nào (ví dụ
> trong constructor, hoặc object trên stack) → ném `bad_weak_ptr`.

### 7.3 Vì sao move shared_ptr rẻ hơn copy

- **copy**: `++strong` — một **atomic** operation (đắt, cần đồng bộ giữa core).
- **move**: chuyển 2 con trỏ + để nguồn về nullptr (`std::exchange`) — **KHÔNG đụng
  strong count**, vì tổng số chủ không đổi (chỉ chuyển quyền).

Nên khi trả shared_ptr từ hàm, hoặc đưa vào container, **move** nó → tránh atomic thừa.
Đây là W1/W4 áp dụng: `std::move(sptr)` khi không cần bản gốc nữa.

### 7.4 `shared_ptr<T[]>` — delete hay delete[]

Từ **C++17**, `shared_ptr<T[]>` xử lý đúng: destructor gọi `delete[]`, và có `operator[]`.
Trước C++17 phải tự cấp custom deleter `[](T* p){ delete[] p; }`. `unique_ptr<T[]>` có từ
C++11. (Nhưng thường `std::vector` tốt hơn cả hai cho mảng động.)

### 7.5 Vì sao weak_ptr không có `operator*` / `->`

Vì object weak_ptr trỏ tới **có thể đã bị huỷ bất cứ lúc nào** (weak không giữ nó sống).
Nếu cho `*w` trực tiếp, bạn có thể dereference object đã chết → UB. Bắt buộc `.lock()`
trước: nó **atomic** kiểm object còn sống + tăng strong, trả shared_ptr an toàn (hoặc
rỗng). Không có đường tắt bỏ qua bước kiểm này → an toàn by design.

### 7.6 Aliasing constructor

`shared_ptr<U>(shared_ptr<T> owner, U* ptr)` — chia sẻ **quyền sở hữu** của `owner`
(dùng chung control block, đếm chung) nhưng `get()` trả về `ptr` (thường là **member**
của object mà owner quản).

Dùng để: trả shared_ptr tới một *member* của object, mà vẫn giữ **cả object** sống.
```cpp
struct Big { Header h; Data d; };
auto big = std::make_shared<Big>();
std::shared_ptr<Header> hdr(big, &big->h);   // trỏ h, nhưng giữ CẢ Big sống
```
`hdr` sống → cả `Big` sống (dù bạn chỉ quan tâm `h`). Control block đếm chung nên `Big`
không bị huỷ khi `big` chết mà `hdr` còn.
