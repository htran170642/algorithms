# Interview Question Bank

Tổ chức **theo chủ đề**, không theo tuần — vì interviewer hỏi *"anh biết gì về memory trong
embedded?"*, chứ không hỏi *"tuần 1 anh học gì?"*. Một tuần có thể bổ sung vào nhiều file; một file
lớn dần qua nhiều tuần.

## Định dạng mỗi câu

Ở đây **không** viết đáp án đầy đủ — đáp án đầy đủ nằm trong `notes/`. Ở đây chỉ có:

- **Ý chính phải nói** — dàn ý để tự kiểm tra, che đi rồi nói lại
- **Bẫy** — câu trả lời nghe hợp lý nhưng làm mất điểm
- **Đào sâu** — thứ interviewer hỏi tiếp nếu bạn trả lời tốt
- **Nguồn** — link tới note có đáp án đầy đủ

Lý do tách như vậy: đọc lại đáp án đầy đủ tạo *cảm giác* đã thuộc mà không kiểm tra được gì. Dàn ý
ép bạn phải tự tái tạo nội dung — đó mới là recall thật (CLAUDE.md §5 bước 2).

## File

| File | Chủ đề | Tuần đóng góp |
|---|---|---|
| [cpp_memory.md](cpp_memory.md) | Memory, lifetime, allocation, exception safety | W1 |
| *(sắp có)* `cpp_concurrency.md` | Thread, atomic, priority inversion, RT | W2, W6 |
| *(sắp có)* `cpp_embedded.md` | volatile, MMIO, endianness, bit ops, ABI | W3 |
| *(sắp có)* `linux_ipc.md` | Process, IPC, epoll, scheduling | W4–W6 |
| *(sắp có)* `automotive_network.md` | CAN, CAN-FD, LIN, Ethernet, SOME/IP, UDS/DoIP | W8–W11 |
| *(sắp có)* `cdc_architecture.md` | CDC, hypervisor, mixed criticality, AAOS/VHAL | W12–W14 |
| *(sắp có)* `autosar.md` | Classic vs Adaptive, ara::com, ara::diag | W19–W21 |
| *(sắp có)* `safety_security.md` | ISO 26262, ASIL, HARA, ISO 21434, secure boot | W28 |

## Thang tự đánh giá

| | Dấu hiệu |
|---|---|
| ✅ Đã vào | Nói được **cơ chế**, và trả lời được câu "đào sâu" |
| ⚠️ Thuộc lòng | Đọc đúng chữ nhưng bí khi bị hỏi *"tại sao lại thế?"* |
| ❌ Chưa vào | Rơi vào **bẫy** |
