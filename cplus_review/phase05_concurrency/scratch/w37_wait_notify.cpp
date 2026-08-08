// phase05_concurrency/scratch/w37_wait_notify.cpp
// W37 — wait()/notify() thay busy-wait. Đồng bộ VẪN là release/acquire.
//   Producer: data=42; ready.store(true,release); ready.notify_one();
//   Consumer: ready.wait(false, acquire)  -> NGỦ (0% CPU) tới khi bị đánh thức.

#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>

int main() {
    std::atomic<bool> ready{false};
    int               data = 0;   // biến THƯỜNG, cờ bảo vệ

    std::jthread producer([&] {
        data = 42;                                        // (1) ghi dữ liệu
        ready.store(true, std::memory_order_release);     // (2) release: (1) không rớt xuống
        ready.notify_one();                               // (3) đánh thức consumer đang ngủ
    });

    ready.wait(false, std::memory_order_acquire);         // (4) ngủ khi CÒN false; acquire-load
    assert(data == 42);                                   // đảm bảo thấy 42

    std::cout << "data = " << data << "  (wait/notify OK)\n";
    return 0;
}
