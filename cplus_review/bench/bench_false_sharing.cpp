// W31 — the number behind "two threads, two variables, still slow."
//
// Each of kThreads threads hammers fetch_add on its OWN counter — logically
// zero contention. But if the counters sit in the same 64-byte cache line,
// every write on core A invalidates the line cached on core B: the line
// ping-pongs across the coherence fabric (MESI) as if the threads shared one
// variable. That is FALSE sharing — no data race, no wrong answer, just cache
// lines bouncing. The fix is spatial, not logical: give each hot counter its
// own line with alignas(64) (== std::hardware_destructive_interference_size on
// x86-64; hardcoded to dodge GCC's -Winterference-size under -Werror).
//
//   BM_Packed : counters adjacent → 4 share one line → heavy ping-pong.
//   BM_Padded : each counter alignas(64) → one line each → no false sharing.
//
// Ratio is typically 3–10x. Run RELEASE:
//   cmake --preset release-23 && cmake --build --preset release-23
//   ./build/release-23/bench/bench_false_sharing

#include <benchmark/benchmark.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace {

constexpr int      kThreads = 4;
constexpr long     kIters   = 20'000'000;
constexpr std::size_t kLine = 64;  // cache line on x86-64

// Counters packed back-to-back: 8 bytes each, 8 per line → all 4 share ONE line.
struct Packed {
    std::atomic<std::int64_t> c[kThreads];
    std::atomic<std::int64_t>& at(int i) { return c[i]; }
};

// Each counter forced onto its own cache line → threads never touch a shared line.
struct Padded {
    struct alignas(kLine) Cell {
        std::atomic<std::int64_t> v;
    };
    Cell c[kThreads];
    std::atomic<std::int64_t>& at(int i) { return c[i].v; }
};

template <typename Layout>
void hammer(benchmark::State& state) {
    for (auto _ : state) {
        Layout layout{};
        for (int i = 0; i < kThreads; ++i) layout.at(i).store(0, std::memory_order_relaxed);

        std::vector<std::thread> threads;
        threads.reserve(kThreads);
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&layout, t] {
                for (long i = 0; i < kIters; ++i) {
                    layout.at(t).fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        for (auto& th : threads) th.join();
        benchmark::DoNotOptimize(layout.at(0).load(std::memory_order_relaxed));
    }
}

void BM_Packed(benchmark::State& state) { hammer<Packed>(state); }
void BM_Padded(benchmark::State& state) { hammer<Padded>(state); }

BENCHMARK(BM_Packed)->UseRealTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Padded)->UseRealTime()->Unit(benchmark::kMillisecond);

}  // namespace
