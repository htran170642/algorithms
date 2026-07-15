# W4 — Move Semantics · SSO · Copy-and-Swap

> Mục **4** để trống — bạn đã trả lời đúng A-A-B trong buổi học. Viết lại bằng lời bạn.

---

## 0. Bằng chứng (tự đo)

```
sizeof(String)              = 32 byte
&s (object, tren stack)     = 0x...6430
s.c_str() (du lieu "hi")    = 0x...6440   → cách 16 byte
```
→ dữ liệu chuỗi ngắn nằm **bên trong** object, không phải trên heap.

```
chuoi NGAN "hi"          -> heap_allocs = 0    ← SSO: không cấp phát
chuoi DAI  (51 ky tu)    -> heap_allocs = 1    ← quá 15 ký tự → heap
truoc move               -> heap_allocs = 1
sau  move (cuop con tro) -> heap_allocs = 1    ← KHÔNG tăng
```

---

## 1. SSO — Small String Optimization

`std::string s = "hi";` cấp phát **0 byte heap**. Chuỗi ngắn nằm trong một buffer
nhỏ **ngay bên trong object** (~15 ký tự), trên stack.

**Cơ chế:** một con trỏ `data_` luôn trỏ tới dữ liệu thật —
- ngắn → `data_` trỏ vào `buf_` (chính bản thân object)
- dài → `data_` trỏ ra heap

Mọi hàm đọc chỉ dùng `data_`, không cần biết đang ở chế độ nào. Chỉ `~String()` và
`swap` mới phải phân biệt.

**Vì sao đáng:** cấp phát heap đắt (~100ns) và phá cache locality. Đa số string đời
thực đều ngắn (tên biến, key, path). SSO biến trường hợp phổ biến nhất thành miễn phí.

**Cái giá:**
| SSO cho | SSO lấy đi |
|---|---|
| chuỗi ngắn: 0 cấp phát | `sizeof` phình to (32 byte) |
| cache-friendly | **move ngắn = copy ngắn** (không có con trỏ để cướp) |
| | swap/move phức tạp (con trỏ trỏ-vào-chính-mình) |

---

## 2. Move: dài thì cướp, ngắn thì copy

```cpp
void moveFrom(String& other) noexcept {
    size_ = other.size_;
    if (other.isShort()) {
        std::memcpy(buf_, other.buf_, kBufSize);   // NGẮN: phải copy 16 byte
        data_ = buf_;
    } else {
        data_ = other.data_;         // DÀI: cướp con trỏ heap (8 byte)
        other.data_ = other.buf_;    // để nguồn lại HỢP LỆ (nếu quên → double free)
    }
    other.size_ = 0;
    other.buf_[0] = '\0';
}
```

> **Move một chuỗi ngắn KHÔNG rẻ hơn copy** — dữ liệu nằm trong `buf_` của nguồn,
> không có con trỏ nào để cướp, phải memcpy cả 16 byte.

`other.data_ = other.buf_` (nhánh dài): sau khi cướp heap của nguồn, phải trỏ `data_`
của nguồn về `buf_` của chính nó → nguồn thành chuỗi rỗng hợp lệ. Quên dòng này →
destructor của nguồn `delete[]` vào con trỏ ta vừa cướp → **double free**. Cùng bài
học `ScopedFile` (W3): nguồn sau move phải hợp lệ và không huỷ cái đã bị lấy đi.

---

## 3. `swap` với SSO — vì sao không tầm thường

```cpp
friend void swap(String& a, String& b) noexcept {
    // hoán 16 byte buf_ ...
    char* adata = a.isShort() ? a.buf_ : b.data_;   // ← không thể chỉ hoán data_
    char* bdata = b.isShort() ? b.buf_ : a.data_;
    a.data_ = adata;
    b.data_ = bdata;
}
```

`data_` của chuỗi ngắn **trỏ vào `buf_` của chính object**. Sau khi hoán `buf_`, nếu
`a` giờ là chuỗi ngắn thì `data_` của nó phải trỏ vào `a.buf_` (địa chỉ của `a`), không
phải con trỏ cũ nhận từ `b`. Đây là cái giá của SSO, và là lý do `swap` của
`std::string` thật cũng phức tạp hơn tưởng.

---

## 4. Copy-and-swap — **TỰ VIẾT**

```cpp
String& operator=(String other) noexcept {   // BY VALUE
    swap(*this, other);
    return *this;
}
```

Bạn đã trả lời A-A-B đúng. Viết lại bằng lời:
- Bản sao `other` được tạo ở **thời điểm nào**? (trước hay sau thân hàm)
- Nếu `new` ném khi tạo `other`, `*this` đã bị đụng chưa? → đây là guarantee gì?
- `delete[]` dữ liệu cũ cuối cùng ai làm, ở đâu?
- Vì sao nó **tự động** self-assignment-safe, không cần `if (this != &other)`?

**Trả lời:**




---

## 5. Câu hỏi phỏng vấn tiếp theo

- Vì sao `operator=(String other)` xử lý được **cả** copy-assign lẫn move-assign chỉ
  bằng một hàm? (gợi ý: `other` được khởi tạo bằng copy ctor hay move ctor tuỳ vào
  argument là lvalue hay rvalue)
- SSO ảnh hưởng thế nào tới hiệu năng của **move**? (bạn đã đo — nói thành lời)
- Nếu `kBufSize` = 1 (tắt SSO hiệu quả), điều gì đổi? Khi nào SSO **phản tác dụng**?
- `std::string` của libstdc++ `sizeof` = 32. Của libc++ thì khác. Vì sao implementation
  lại chọn kích thước SSO khác nhau?
