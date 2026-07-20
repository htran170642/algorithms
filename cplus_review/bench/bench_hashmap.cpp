// W14 — does an open-addressing map beat std::unordered_map at lookup?
// The standard forces unordered_map to be node-based; we aren't. Measure.

#include <benchmark/benchmark.h>

#include <unordered_map>
#include <vector>

#include "hash_map.hpp"

namespace {

constexpr int kN = 100000;

void BM_LookupOurs(benchmark::State& state) {
    cr::HashMap<int, int> m(1 << 18);
    for (int i = 0; i < kN; ++i) m.insert(i, i);
    for (auto _ : state) {
        long sum = 0;
        for (int i = 0; i < kN; ++i) sum += m.find(i).value_or(0);
        benchmark::DoNotOptimize(sum);
    }
}
BENCHMARK(BM_LookupOurs);

void BM_LookupStd(benchmark::State& state) {
    std::unordered_map<int, int> m;
    m.reserve(kN);
    for (int i = 0; i < kN; ++i) m[i] = i;
    for (auto _ : state) {
        long sum = 0;
        for (int i = 0; i < kN; ++i) sum += m.at(i);
        benchmark::DoNotOptimize(sum);
    }
}
BENCHMARK(BM_LookupStd);

}  // namespace
