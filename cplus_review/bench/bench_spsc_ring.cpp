// W41 — benchmark a REAL component, not a synthetic loop.
//
// bench_all.cpp taught the DoNotOptimize trap on a single-threaded accumulate.
// bench_false_sharing.cpp (W31) proved alignas(64) on synthetic counters.
// This file closes the loop: it measures the actual W38 SpscRing, and answers
// the "What would a Staff Engineer improve?" question with a NUMBER — does the
// alignas(64) padding on head_/tail_ really buy anything on the real ring?
//
// Two things a threaded throughput benchmark must get right (the W41 discipline):
//   * UseRealTime()  — CPU-time sums across threads; we want wall-clock.
//   * SetItemsProcessed(kN) — report items/sec, the metric that actually means
//     something for a queue, instead of ns/iter of an opaque outer loop.
//
// ALWAYS run the release preset — a -O0 build measures the absent optimizer:
//   cmake --preset release-23 && cmake --build --preset release-23
//   ./build/release-23/bench/bench_spsc_ring

#include "phase05_concurrency/include/spsc_ring.hpp"

#include <benchmark/benchmark.h>

#include <atomic>
#include <cstdint>
#include <new>
#include <thread>

namespace {

constexpr std::int64_t kN        = 4'000'000;   // items pumped per benchmark iteration
constexpr std::size_t  kCapacity = 1024;

// --- 1) The real component -------------------------------------------------
// One producer pushes 0..kN-1 (spin while full); one consumer pops kN (spin
// while empty). This is exactly the W38 stress test, now timed for throughput.
void BM_SpscRing_Real(benchmark::State& state) {
    for (auto _ : state) {
        cr::SpscRing<std::int64_t, kCapacity> ring;
        std::thread consumer([&] {
            std::int64_t v = 0;
            for (std::int64_t got = 0; got < kN; ) {
                if (ring.pop(v)) { benchmark::DoNotOptimize(v); ++got; }
                else             { /* spin: empty */ }
            }
        });
        for (std::int64_t i = 0; i < kN; ) {
            if (ring.push(i)) ++i;   // spin while full
        }
        consumer.join();
    }
    state.SetItemsProcessed(state.iterations() * kN);
}
BENCHMARK(BM_SpscRing_Real)->UseRealTime()->Unit(benchmark::kMillisecond);

// --- 2) Padded vs packed, same ring logic ----------------------------------
// A minimal SPSC ring whose two indices are alignas(Align). Align==64 puts
// head_/tail_ on separate cache lines (the real SpscRing); Align==alignof(...)
// lets them share one line, so the producer's tail_ store keeps invalidating the
// consumer's line — false sharing, on the real hot path this time.
template <std::size_t Align>
class MiniRing {
    static_assert((kCapacity & (kCapacity - 1)) == 0, "power of two");
    static constexpr std::uint64_t kMask = kCapacity - 1;

    struct alignas(Align) Index { std::atomic<std::uint64_t> v{0}; };

public:
    bool push(std::int64_t x) {
        const std::uint64_t t = tail_.v.load(std::memory_order_relaxed);
        if (t - head_.v.load(std::memory_order_acquire) == kCapacity) return false;
        buf_[t & kMask] = x;
        tail_.v.store(t + 1, std::memory_order_release);
        return true;
    }
    bool pop(std::int64_t& out) {
        const std::uint64_t h = head_.v.load(std::memory_order_relaxed);
        if (h == tail_.v.load(std::memory_order_acquire)) return false;
        out = buf_[h & kMask];
        head_.v.store(h + 1, std::memory_order_release);
        return true;
    }

private:
    std::int64_t buf_[kCapacity]{};
    Index head_;   // consumer writes, producer reads
    Index tail_;   // producer writes, consumer reads  (adjacent → same line when packed)
};

template <std::size_t Align>
void BM_SpscRing_Align(benchmark::State& state) {
    for (auto _ : state) {
        MiniRing<Align> ring;
        std::thread consumer([&] {
            std::int64_t v = 0;
            for (std::int64_t got = 0; got < kN; ) {
                if (ring.pop(v)) { benchmark::DoNotOptimize(v); ++got; }
            }
        });
        for (std::int64_t i = 0; i < kN; ) {
            if (ring.push(i)) ++i;
        }
        consumer.join();
    }
    state.SetItemsProcessed(state.iterations() * kN);
}

// alignof(std::atomic<uint64_t>) == 8 → head_ and tail_ pack into one line.
BENCHMARK(BM_SpscRing_Align<alignof(std::atomic<std::uint64_t>)>)
    ->Name("BM_MiniRing_Packed")->UseRealTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SpscRing_Align<64>)
    ->Name("BM_MiniRing_Padded")->UseRealTime()->Unit(benchmark::kMillisecond);

}  // namespace
