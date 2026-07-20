// W13 — the textbook says list wins at mid-insertion. Let's measure.
//
// Run with the RELEASE preset. Benchmarking -O0 measures nothing.

#include <benchmark/benchmark.h>

#include <algorithm>
#include <list>
#include <numeric>
#include <vector>

namespace {

constexpr int kSize = 20000;

// ---- 1. pure traversal: sum every element ----------------------------------
void BM_TraverseVector(benchmark::State& state) {
    std::vector<int> v(kSize);
    std::iota(v.begin(), v.end(), 0);
    for (auto _ : state) {
        long sum = 0;
        for (int x : v) sum += x;
        benchmark::DoNotOptimize(sum);
    }
}
BENCHMARK(BM_TraverseVector);

void BM_TraverseList(benchmark::State& state) {
    std::list<int> l(kSize);
    std::iota(l.begin(), l.end(), 0);
    for (auto _ : state) {
        long sum = 0;
        for (int x : l) sum += x;
        benchmark::DoNotOptimize(sum);
    }
}
BENCHMARK(BM_TraverseList);

// ---- 2. the real question: FIND a position, then insert there ---------------
// This is the honest workload. "list inserts in O(1)" only counts if you
// already hold the iterator — getting there is the expensive part.

void BM_FindAndInsertVector(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        std::vector<int> v(kSize);
        std::iota(v.begin(), v.end(), 0);
        state.ResumeTiming();

        for (int i = 0; i < 100; ++i) {
            auto it = std::find(v.begin(), v.end(), kSize / 2);   // scan (cache-friendly)
            v.insert(it, -i);                                      // memmove the tail
        }
        benchmark::DoNotOptimize(v.data());
    }
}
BENCHMARK(BM_FindAndInsertVector);

void BM_FindAndInsertList(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        std::list<int> l(kSize);
        std::iota(l.begin(), l.end(), 0);
        state.ResumeTiming();

        for (int i = 0; i < 100; ++i) {
            auto it = std::find(l.begin(), l.end(), kSize / 2);   // walk (cache miss each node)
            l.insert(it, -i);                                      // O(1) pointer surgery
        }
        benchmark::DoNotOptimize(&l);
    }
}
BENCHMARK(BM_FindAndInsertList);

// ---- 3. where list actually wins: you ALREADY hold the iterator -------------
//
// NOTE: the first draft of this used pop_back() to keep the size stable, and it
// SEGFAULTED — a live W12 lesson. Inserting before mid keeps the count after mid
// fixed while pop_back shrinks it, so mid drifted until it WAS the last element;
// pop_back then destroyed the node mid pointed at, and the next insert(mid) was a
// use-after-free. Erasing exactly what we inserted keeps both size and position
// stable forever.
void BM_InsertAtKnownPosVector(benchmark::State& state) {
    std::vector<int> v(kSize);
    for (auto _ : state) {
        auto it = v.insert(v.begin() + kSize / 2, 42);   // pure memmove cost
        v.erase(it);                                      // undo, same cost class
        benchmark::DoNotOptimize(v.data());
    }
}
BENCHMARK(BM_InsertAtKnownPosVector);

void BM_InsertAtKnownPosList(benchmark::State& state) {
    std::list<int> l(kSize);
    auto mid = std::next(l.begin(), kSize / 2);   // iterator obtained ONCE, outside the loop
    for (auto _ : state) {
        auto it = l.insert(mid, 42);              // pure O(1) pointer surgery
        l.erase(it);                              // undo — mid stays valid (list is stable)
        benchmark::DoNotOptimize(&l);
    }
}
BENCHMARK(BM_InsertAtKnownPosList);

}  // namespace
