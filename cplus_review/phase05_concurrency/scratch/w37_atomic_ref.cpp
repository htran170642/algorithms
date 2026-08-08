// phase05_concurrency/scratch/w37_atomic_ref.cpp
// W37 — atomic_ref: mượn tính atomic cho phần tử của một vector<int> THƯỜNG.
//   8 thread, mỗi thread fetch_add 1000 lần vào ô riêng -> không data race, tổng đúng.

#include <atomic>
#include <cassert>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

int main() {
    std::vector<int> counters(8, 0);          // int THƯỜNG, không phải atomic

    {
        std::vector<std::jthread> workers;
        for (int i = 0; i < 8; ++i) {
            workers.emplace_back([&counters, i] {
                std::atomic_ref<int> slot{counters[i]};        // khung nhìn atomic lên ô i
                for (int n = 0; n < 1000; ++n)
                    slot.fetch_add(1, std::memory_order_relaxed);  // đếm thuần -> relaxed đủ
            });
        }
    }   // jthread join hết ở đây -> mọi atomic_ref đã chết, counters lại là int thường

    const int total = std::reduce(counters.begin(), counters.end());
    assert(total == 8 * 1000);
    std::cout << "total = " << total << "  (atomic_ref OK)\n";
    return 0;
}
