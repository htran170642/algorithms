// phase05_concurrency/scratch/w37_release_acquire.cpp
// W37 — release/acquire message passing (demo).
//   ready = atomic<bool> : cái CỜ
//   data  = int thường    : DỮ LIỆU được cờ bảo vệ
// Câu thần chú: release + acquire trên CÙNG một biến, và acquire đọc ĐƯỢC giá trị
// release ghi -> mọi thứ TRƯỚC release nhìn thấy được SAU acquire. data khỏi cần atomic.

#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>

int main() {
    std::atomic<bool> ready{false};
    int               data = 0;   // biến THƯỜNG

    std::jthread producer([&] {
        data = 42;                                        // (1) ghi dữ liệu
        ready.store(true, std::memory_order_release);     // (2) release: (1) không rớt xuống dưới
    });

    while (!ready.load(std::memory_order_acquire)) {      // (3) acquire: đọc dưới không leo lên
        // busy-wait
    }
    assert(data == 42);   // ĐẢM BẢO thấy 42, không bao giờ 0

    std::cout << "data = " << data << "  (release/acquire OK)\n";
    return 0;
}
