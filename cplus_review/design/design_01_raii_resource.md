# Design Round #1 — Generic RAII Resource Wrapper

**Boundary:** cuối Phase 1 (W10)
**Đề:** thay vì viết `ScopedFile`, `ScopedSocket`, `ScopedMutex`... riêng lẻ, thiết kế
MỘT type quản lý bất kỳ tài nguyên nào cần "giành lúc tạo, trả lúc huỷ".
Đây gần đúng `std::unique_ptr` + custom deleter / `std::experimental::unique_resource`.

Theo System Design Workflow của CLAUDE.md (11 bước).

---

## 1–2. Requirements & Clarifications

| Câu hỏi làm rõ | Chốt |
|---|---|
| Tài nguyên là gì? | Không cứng là con trỏ → **template `<Handle, Deleter>`** (FILE*, int fd, GLuint...) |
| Copy hay move-only? | **Move-only** — một tài nguyên một chủ; copy → double-free (W3) |
| "Rỗng" biểu diễn sao? | **Người dùng chỉ định `invalid_`** — file: nullptr; fd: -1 (vì 0 = stdin hợp lệ) |

## 3. Constraints
- RAII: giành ở ctor, trả ở dtor, không leak dù có exception (W3)
- Zero-overhead: deleter stateless → 0 byte (EBO / `[[no_unique_address]]`, W8)
- move + dtor **noexcept** → dùng được trong `vector` (W1); dtor không ném (W3)

## 6. Class Design (cốt lõi)

```cpp
template <typename Handle, typename Deleter>
class UniqueResource {
public:
    UniqueResource(Handle h, Deleter d, Handle invalid = Handle{}) noexcept
        : handle_(h), deleter_(std::move(d)), invalid_(invalid) {}

    UniqueResource(const UniqueResource&)            = delete;   // W3 move-only
    UniqueResource& operator=(const UniqueResource&) = delete;

    UniqueResource(UniqueResource&& o) noexcept                  // W4 cướp handle
        : handle_(std::exchange(o.handle_, o.invalid_)),
          deleter_(std::move(o.deleter_)), invalid_(o.invalid_) {}

    UniqueResource& operator=(UniqueResource&& o) noexcept {
        if (this != &o) {
            reset();
            handle_  = std::exchange(o.handle_, o.invalid_);
            deleter_ = std::move(o.deleter_);
            invalid_ = o.invalid_;
        }
        return *this;
    }

    ~UniqueResource() { reset(); }                              // W3 RAII

    void reset() noexcept {                                     // W3 dtor không ném
        if (handle_ != invalid_) { deleter_(handle_); handle_ = invalid_; }
    }

    [[nodiscard]] Handle get()   const noexcept { return handle_; }        // W9
    [[nodiscard]] bool   valid() const noexcept { return handle_ != invalid_; }

private:
    Handle handle_;
    [[no_unique_address]] Deleter deleter_;   // W8 EBO → deleter rỗng 0 byte
    Handle invalid_;
};
```

**Mọi tuần Phase 1 hội tụ ở đây:**
W1 noexcept · W3 RAII + move-only · W4 std::exchange · W8 EBO · W9 [[nodiscard]].

## Bằng chứng (đã chạy, sạch ASAN+UBSAN)
```
Move fileA -> fileC:  fileA.get()=-1 (rỗng)   fileC.get()=3 (giữ)
Ra scope:  close(fd=3)  close(fd=5)   ← fileA KHÔNG double-close
sizeof(UniqueResource<int,lambda>) = 8   ← 2 int, deleter 0 byte (EBO)
```

## 5. Trade-offs
- **Deleter là template param vs `std::function`:** template param = 0 overhead nhưng
  mỗi deleter là một kiểu khác; `std::function` = cùng kiểu nhưng tốn heap + virtual
  call. `unique_ptr` chọn template param (hiệu năng). ← câu trade-off hay nhất.
- `invalid_` tốn thêm 1 field nhưng bắt buộc để đúng với fd/texture.

## 9. Failure Handling
- Handle hỏng lúc vào (`fopen`→nullptr): không tự kiểm, `reset()` không làm gì nếu
  `==invalid_`. Kiểm mở-thành-công là việc người gọi.
- Deleter ném: cấm (reset noexcept, gọi trong dtor). Deleter phải nuốt lỗi.
- Self-move: `if (this != &o)` chặn.

## 11. Interview Follow-up
- Thêm `release()` (nhả quyền, không đóng): trả `handle_`, đặt `handle_ = invalid_`.
- Vì sao `unique_ptr` mặc định `default_delete` chứ không lambda?
- Thread-safe không? Không — nhưng có *cần* không? → Phase 5.

---

**Chốt:** Phase 1 được thiết kế để hội tụ về bài toán này — quản lý tài nguyên an toàn
là bài toán trung tâm của C++, và 10 tuần vừa qua đều là công cụ để giải nó.
