// W25 — the number behind "CRTP is faster than virtual."
//
// Honest framing: this measures the TOTAL cost of each style, not dispatch in
// isolation. Virtual polymorphism REQUIRES a base pointer, which forces the
// objects onto the heap (vector<unique_ptr<Shape>>) and scatters them — so the
// virtual run pays vtable indirection AND pointer chasing AND lost inlining.
// That bundle is exactly what you pay in real code; isolating one strand would
// measure a program nobody writes.
//
// Caveat (ROADMAP Week 0 debt): CPU frequency scaling is still on, so absolute
// ns are noisy until W41. The RATIO between the two bars is the takeaway.
//
// Run the RELEASE build — a -O0 measurement of inlining measures nothing:
//   cmake --preset release-23 && cmake --build --preset release-23
//   ./build/release-23/bench/bench_crtp

#include <benchmark/benchmark.h>

#include <memory>
#include <vector>

#include "w25/shapes.hpp"

namespace {

constexpr int kN = 10'000;

// Virtual: heterogeneous-capable, hence heap + base pointers + vtable.
void BM_VirtualDispatch(benchmark::State& state) {
    std::vector<std::unique_ptr<w25::Shape>> shapes;
    shapes.reserve(kN);
    for (int i = 0; i < kN; ++i) {
        shapes.push_back(std::make_unique<w25::Circle>(i * 0.001 + 1.0));
    }
    for (auto _ : state) {
        double sum = 0.0;
        for (const auto& s : shapes) {
            sum += s->area();               // indirect call through the vtable
        }
        benchmark::DoNotOptimize(sum);
    }
}
BENCHMARK(BM_VirtualDispatch);

// CRTP: monomorphic by construction — values sit contiguously, area() inlines.
void BM_CrtpStatic(benchmark::State& state) {
    std::vector<w25::CircleS> shapes;
    shapes.reserve(kN);
    for (int i = 0; i < kN; ++i) {
        shapes.emplace_back(i * 0.001 + 1.0);
    }
    for (auto _ : state) {
        double sum = 0.0;
        for (const auto& s : shapes) {
            sum += s.area();                // resolved at compile time, inlined
        }
        benchmark::DoNotOptimize(sum);
    }
}
BENCHMARK(BM_CrtpStatic);

}  // namespace
