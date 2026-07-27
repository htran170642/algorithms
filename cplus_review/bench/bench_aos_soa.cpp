// W31 — the number behind "layout beats cleverness when you stream few fields."
//
// Workload: integrate position from velocity over N particles — the hot loop
// touches only x,y,z,vx,vy,vz (24 bytes) of a 64-byte particle. The other 40
// bytes (mass, color, flags, AI state...) are COLD this pass.
//
//   AoS  (vector<Particle>) : to read 24 useful bytes the CPU must load the
//                             whole 64-byte line → ~37% of cache bandwidth is
//                             spent on fields this loop never reads.
//   SoA  (struct of arrays) : x[], y[], z[], vx[], vy[], vz[] each stream at
//                             100% useful bytes/line → ~2.6x fewer lines pulled,
//                             and each array vectorizes cleanly.
//
// The takeaway is the RATIO and the perf cache-miss delta, not absolute ns
// (CPU freq scaling still on — ROADMAP Week 0 debt, fixed at W41).
//
// Run RELEASE, and prove the cache story with perf:
//   cmake --preset release-23 && cmake --build --preset release-23
//   ./build/release-23/bench/bench_aos_soa
//   perf stat -e cache-references,cache-misses,LLC-load-misses ./build/release-23/bench/bench_aos_soa --benchmark_filter=BM_AoS
//   perf stat -e cache-references,cache-misses,LLC-load-misses ./build/release-23/bench/bench_aos_soa --benchmark_filter=BM_SoA

#include <benchmark/benchmark.h>

#include <array>
#include <vector>

namespace {

constexpr std::size_t kN  = 1 << 20;  // ~1M particles
constexpr float       kDt = 0.016f;   // one 60 Hz frame

// 64 bytes: 6 hot floats + 40 bytes of fields this loop does not touch.
struct Particle {
    float x, y, z;      // position — HOT
    float vx, vy, vz;   // velocity — HOT
    float cold[10];     // mass, color, flags, ai-state... — COLD this pass
};
static_assert(sizeof(Particle) == 64, "one particle per cache line");

// Array-of-Structs: every field of a particle sits together. The integrate
// loop drags the cold 40 bytes through cache on every element.
void BM_AoS(benchmark::State& state) {
    std::vector<Particle> ps(kN);
    for (std::size_t i = 0; i < kN; ++i) {
        ps[i].vx = 1.0f; ps[i].vy = 2.0f; ps[i].vz = 3.0f;
    }
    for (auto _ : state) {
        for (std::size_t i = 0; i < kN; ++i) {
            ps[i].x += ps[i].vx * kDt;
            ps[i].y += ps[i].vy * kDt;
            ps[i].z += ps[i].vz * kDt;
        }
        benchmark::DoNotOptimize(ps.data());
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_AoS);

// Struct-of-Arrays: each field is its own contiguous array. The integrate loop
// streams only the six arrays it uses; cold data never enters cache.
struct ParticlesSoA {
    std::vector<float> x, y, z, vx, vy, vz;
    std::vector<std::array<float, 10>> cold;  // present, but never touched here
    explicit ParticlesSoA(std::size_t n)
        : x(n), y(n), z(n), vx(n, 1.0f), vy(n, 2.0f), vz(n, 3.0f), cold(n) {}
};

void BM_SoA(benchmark::State& state) {
    ParticlesSoA ps(kN);
    for (auto _ : state) {
        for (std::size_t i = 0; i < kN; ++i) {
            ps.x[i] += ps.vx[i] * kDt;
            ps.y[i] += ps.vy[i] * kDt;
            ps.z[i] += ps.vz[i] * kDt;
        }
        benchmark::DoNotOptimize(ps.x.data());
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_SoA);

}  // namespace
