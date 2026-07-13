// Google Benchmark harness. Comes online in Week 0 because W13 (list vs vector)
// and W31 (false sharing) are claims you must MEASURE, not take on faith.
//
// ALWAYS benchmark the release preset:
//     cmake --build --preset release-23
//     ./build/release-23/bench/bench_all
//
// Benchmarking a debug build measures nothing but the absence of the optimizer.

#include <benchmark/benchmark.h>

#include <numeric>
#include <vector>

namespace {

std::vector<int> makeData() {
    std::vector<int> v(10'000);
    std::iota(v.begin(), v.end(), 0);
    return v;
}

// THE TRAP. The result is unused, so at -O2 the optimizer is entitled to delete
// the entire accumulate call. You end up timing an empty loop and concluding
// your code is infinitely fast.
void BM_SumButOptimizedAway(benchmark::State& state) {
    const std::vector<int> data = makeData();
    for (auto _ : state) {
        long sum = std::accumulate(data.begin(), data.end(), 0L);
        (void)sum;                          // NOT enough. The compiler knows.
    }
}
BENCHMARK(BM_SumButOptimizedAway);

// THE FIX. DoNotOptimize() tells the compiler the value escapes, so the work
// must actually happen. Compare the two numbers -- the gap IS the lesson.
void BM_SumMeasuredHonestly(benchmark::State& state) {
    const std::vector<int> data = makeData();
    for (auto _ : state) {
        long sum = std::accumulate(data.begin(), data.end(), 0L);
        benchmark::DoNotOptimize(sum);      // now it's real
    }
}
BENCHMARK(BM_SumMeasuredHonestly);

}  // namespace
