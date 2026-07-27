// W30 — the number behind "a pool beats new/delete for churn."
//
// Workload: allocate a batch of same-typed nodes, then free them, repeatedly —
// the create/destroy churn a std::list or an object graph produces. new/delete
// routes every call through the general allocator (search, per-block metadata,
// possibly a lock); the pool answers each from a free-list in O(1).
//
// Caveat (ROADMAP Week 0 debt): CPU frequency scaling is still on, so absolute
// ns are noisy until W41. The RATIO between the two bars is the takeaway.
//
// Run the RELEASE build:
//   cmake --preset release-23 && cmake --build --preset release-23
//   ./build/release-23/bench/bench_pool

#include <benchmark/benchmark.h>

#include <vector>

#include "w30/pool.hpp"

namespace {

// A small, same-sized node — the case pools exist for.
struct Node {
    int  key;
    int  value;
    Node* left;
    Node* right;
};

constexpr int kBatch = 1'000;

// Baseline: general-purpose new/delete for every node.
void BM_NewDelete(benchmark::State& state) {
    std::vector<Node*> live;
    live.reserve(kBatch);
    for (auto _ : state) {
        for (int i = 0; i < kBatch; ++i) {
            live.push_back(new Node{i, i * 2, nullptr, nullptr});
        }
        for (Node* p : live) {
            delete p;
        }
        live.clear();
    }
}
BENCHMARK(BM_NewDelete);

// Pool: each create/destroy is a free-list pop/push, slots recycled and hot.
void BM_Pool(benchmark::State& state) {
    std::vector<Node*> live;
    live.reserve(kBatch);
    w30::ObjectPool<Node> pool{kBatch};
    for (auto _ : state) {
        for (int i = 0; i < kBatch; ++i) {
            live.push_back(pool.create(Node{i, i * 2, nullptr, nullptr}));
        }
        for (Node* p : live) {
            pool.destroy(p);
        }
        live.clear();
    }
}
BENCHMARK(BM_Pool);

}  // namespace
