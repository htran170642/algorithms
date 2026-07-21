// W20 — how much faster is a monotonic buffer than the default allocator?
//
// LESSON: pmr helps when there are MANY small allocations. A vector does only
// ~log2(n) allocations (geometric growth, W5), so pmr barely helps there — the
// first draft of this benchmark actually showed pmr LOSING, because each
// iteration also zero-initialized an 8KB stack buffer. A node-based container
// (list) allocates once PER ELEMENT, which is where bump-pointer allocation
// crushes new. That's the honest comparison below.

#include <benchmark/benchmark.h>

#include <cstddef>
#include <list>
#include <memory>
#include <memory_resource>

namespace {

constexpr int kN = 1000;

// default allocator: every list node is a separate ::operator new
void BM_ListDefault(benchmark::State& state) {
    for (auto _ : state) {
        std::list<int> l;
        for (int i = 0; i < kN; ++i) l.push_back(i);
        benchmark::DoNotOptimize(&l);
    }
}
BENCHMARK(BM_ListDefault);

// monotonic buffer: every node is a pointer bump, and nothing is ever freed
// individually — the whole arena is dropped at once. No per-node new/delete.
void BM_ListMonotonic(benchmark::State& state) {
    auto buf = std::make_unique<std::byte[]>(kN * 64);   // heap buffer, allocated ONCE
    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource pool{buf.get(), kN * 64};
        std::pmr::list<int> l{&pool};
        for (int i = 0; i < kN; ++i) l.push_back(i);
        benchmark::DoNotOptimize(&l);
    }
}
BENCHMARK(BM_ListMonotonic);

}  // namespace
