# Mock Interview #2 — STL (Phase 2)

**Chế độ:** teaching-through
**Điểm:** ~5.8/10 (bằng Mock #1)
**Điểm mạnh xác nhận lần 2:** đọc code + dangling/lifetime.
**Điểm yếu:** dừng ở "cái gì", chưa tới "vì sao"; đảo chiều sở hữu smart pointer.

> Ôn: tự trả lời trước, rồi xem đáp án.

---

## Câu 1 — iterator invalidation · *bí (dù đã đo ở W12)*
```cpp
for (auto it = v.begin(); it != v.end(); ++it)
    if (*it > threshold) v.erase(it);   // BUG
```
`v.erase(it)` làm `it` dangling → `++it` dùng iterator chết → UB.
**Sửa:** `it = v.erase(it)` (dùng iterator erase trả về), else `++it`. Hoặc C++20
`std::erase_if(v, pred)`.

## Câu 2 — map vs unordered_map · *chọn đúng, thiếu "vì sao"*
`unordered_map` vì lookup O(1) vs O(log n), và không cần thứ tự.
**Chọn `map` khi:** (1) duyệt theo thứ tự, (2) range query lower/upper_bound, (3)
worst-case O(log n) đảm bảo. Ba thứ map đánh đổi tốc độ để có.

## Câu 3 — string_view + realloc · *TRẢ LỜI HAY NHẤT ✅*
```cpp
std::string_view first = names[0];   // view, không sở hữu (W18)
names.push_back(...);                // đủ nhiều → vector realloc (W12)
std::cout << first;                  // buffer cũ đã free → rác/crash
```
Nối W12 + W18: view dangling sau khi vector dời buffer. ASAN báo heap-use-after-free.

## Câu 4 — hướng sở hữu smart pointer · *ĐẢO CHIỀU*
Cây: cha→con dùng **shared_ptr** (xuống), con→cha dùng **weak_ptr** (lên).
> Sở hữu chảy MỘT chiều, từ trên xuống. Cha giữ con sống; con nhìn lên nhưng không giữ.
**Cách nhớ:** "nếu chỉ giữ root, mọi node có sống không?" → shared phải chảy xuống.
weak xuống = con không có chủ → chết ngay.
(User đúng "cần weak tránh cycle", chỉ đặt nhầm chiều — nghĩ "ai giữ ai sống", không
phải "con trỏ chỉ hướng nào".)

---

## Cần làm trước Phase 3
- [ ] Luyện "vì sao" thành phản xạ: "X vì cho A, tôi cần A không cần B mà Y đánh đổi"
- [ ] Viết lại note: iterator invalidation (W12) + hướng sở hữu smart ptr (W19)
- [ ] Giữ điểm mạnh dangling/lifetime — Phase 4 (Memory) chơi vào sở trường này
