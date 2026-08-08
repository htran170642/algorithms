// W42 — the number behind "the branch predictor is a real machine you can starve."
//
// W41 taught how to MEASURE throughput honestly. W42 looks UNDER it: the same
// arithmetic runs 3-6x slower purely because of *data order*, not work done.
//
// Workload: sum the elements >= 128 of an array of bytes 0..255.
//   Sorted : the `if (v >= 128)` is false for a long run, then true for a long
//            run — the predictor learns it and hits ~100%. Pipeline never flushes.
//   Random : the branch is a coin flip → ~50% mispredict → ~15-20 cycle flush
//            EVERY OTHER element. Same adds, same loads, same cache pattern
//            (both walk memory sequentially) — the ONLY difference is prediction.
//
// This is the famous "why is processing a sorted array faster" effect — but a
// MODERN twist: at -O2 GCC 13 quietly turns `if (v>=128) sum+=v` into a branchless
// `cmov` (if-conversion), so the gap DISAPPEARS. To even see a branch predictor we
// must forbid if-conversion on one variant (`__attribute__((optimize(...)))`).
// So this file tells the whole truth, in one binary:
//   BM_Branch_Sorted / _Random  — branch FORCED to survive; order matters 5-6x.
//   BM_Cmov_Random              — plain -O2; compiler if-converts to cmov → fast.
//   BM_Mask_Random              — hand-written branchless mask → what cmov emits.
//
// Run RELEASE (a -O0 build hides the pipeline story), then read the CPU counters:
//   cmake --preset release-23 && cmake --build --preset release-23
//   ./build/release-23/bench/bench_branch_predict
//   BIN=./build/release-23/bench/bench_branch_predict
//   perf stat -e instructions,cycles,branches,branch-misses $BIN --benchmark_filter=Branch_Sorted
//   perf stat -e instructions,cycles,branches,branch-misses $BIN --benchmark_filter=Branch_Random
// Expect branch-misses ~0.1% (Sorted) vs ~50% (Random); IPC drops in lockstep.

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace {

constexpr std::size_t kN = 1u << 20;  // ~1M bytes — comfortably past L2 into L3

// One fixed random permutation of bytes 0..255, reused by both benchmarks so the
// ONLY difference between them is whether the array is sorted. Built ONCE (a
// static) — construction is setup, it must never be inside the timed loop.
const std::vector<std::uint8_t>& random_data() {
    static const std::vector<std::uint8_t> data = [] {
        std::vector<std::uint8_t> v(kN);
        std::mt19937 gen(42);  // fixed seed → reproducible across runs
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& x : v) x = static_cast<std::uint8_t>(dist(gen));
        return v;
    }();
    return data;
}

// The hot loop with the branch FORCED to survive. The optimize attribute turns
// OFF if-conversion + vectorization for this function only (everything else stays
// -O2), so `if (v >= 128)` compiles to a real conditional jump — the one thing the
// branch predictor actually predicts. Without this attribute GCC 13 emits a cmov
// and there is nothing to mispredict (see sum_big_cmov below).
__attribute__((optimize("no-tree-vectorize", "no-tree-loop-if-convert",
                        "no-if-conversion", "no-if-conversion2")))
long sum_big_branchy(const std::vector<std::uint8_t>& data) {
    long sum = 0;
    for (std::uint8_t v : data) {
        if (v >= 128) sum += v;   // <-- real branch: predictor lives or dies here
    }
    return sum;
}

// --- A) Random order: predictor guesses ~50% wrong → pipeline flushes -------
void BM_Branch_Random(benchmark::State& state) {
    const std::vector<std::uint8_t>& data = random_data();  // setup: OUTSIDE loop
    for (auto _ : state) {
        long sum = sum_big_branchy(data);
        benchmark::DoNotOptimize(sum);  // W41: without this the loop is DCE'd to 0 ns
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kN));
}
BENCHMARK(BM_Branch_Random)->Unit(benchmark::kMillisecond);

// --- B) Sorted order: same bytes, same work, predictable branch → ~100% hit -
void BM_Branch_Sorted(benchmark::State& state) {
    std::vector<std::uint8_t> data = random_data();  // copy, then sort — setup only
    std::sort(data.begin(), data.end());
    for (auto _ : state) {
        long sum = sum_big_branchy(data);
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kN));
}
BENCHMARK(BM_Branch_Sorted)->Unit(benchmark::kMillisecond);

// --- C) Same source, plain -O2: GCC if-converts the branch to a cmov --------
// Identical loop body, but NO attribute → the compiler emits a branchless cmov.
// On RANDOM data this runs as fast as the sorted branch, because there is no
// branch left to mispredict. This is why the classic demo "stopped working."
long sum_big_cmov(const std::vector<std::uint8_t>& data) {
    long sum = 0;
    for (std::uint8_t v : data) {
        if (v >= 128) sum += v;   // -O2 turns this into cmov, not a jump
    }
    return sum;
}

void BM_Cmov_Random(benchmark::State& state) {
    const std::vector<std::uint8_t>& data = random_data();  // random on purpose
    for (auto _ : state) {
        long sum = sum_big_cmov(data);
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kN));
}
BENCHMARK(BM_Cmov_Random)->Unit(benchmark::kMillisecond);

// --- D) Hand-written branchless mask: what cmov/if-conversion emits ----------
// -(v >= 128) is 0 or -1 (all-ones) → mask keeps v or zeroes it, no jump. Proves
// the fix for an unpredictable branch is to remove the branch, not to sort data.
long sum_big_mask(const std::vector<std::uint8_t>& data) {
    long sum = 0;
    for (std::uint8_t v : data) {
        const long mask = -static_cast<long>(v >= 128);
        sum += (static_cast<long>(v) & mask);
    }
    return sum;
}

void BM_Mask_Random(benchmark::State& state) {
    const std::vector<std::uint8_t>& data = random_data();  // random on purpose
    for (auto _ : state) {
        long sum = sum_big_mask(data);
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kN));
}
BENCHMARK(BM_Mask_Random)->Unit(benchmark::kMillisecond);

}  // namespace
