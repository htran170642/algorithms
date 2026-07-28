#include "phase05_concurrency/include/counter.hpp"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

// W32 — data race, seen two ways: a wrong final count AND a TSan report.
//
// Run the clean test in the normal matrix (it must pass under tsan-20/-23):
//   cmake --preset tsan-23 && cmake --build --preset tsan-23
//   ctest --preset tsan-23 --output-on-failure
//
// Then make TSan FIRE on purpose (disabled test, opt-in). The root CMake already
// wraps the binary in `setarch -R` under TSan, so no ASLR flakiness. Run the
// binary directly (one line):
//   setarch -R ./build/tsan-23/phase05_concurrency/phase05_concurrency_test --gtest_also_run_disabled_tests --gtest_filter='*RacyCounterRace*'
// TSan prints "WARNING: ThreadSanitizer: data race" with BOTH stacks pointing at
// RacyCounter::inc.

namespace {

constexpr int  kThreads = 8;
constexpr long kIters   = 100'000;

// Fan out kThreads jthreads, each calling counter.inc() kIters times.
// std::jthread (C++20) is RAII: its destructor joins automatically. So when
// `workers` leaves scope every thread is joined — even if an assertion below
// throws first. A raw std::thread left joinable at destruction calls
// std::terminate(); jthread is the fix (quiz Q1).
template <typename Counter>
void hammer(Counter& counter) {
    std::vector<std::jthread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&counter] {
            for (long i = 0; i < kIters; ++i) {
                counter.inc();
            }
        });
    }
    // workers destructor here -> every jthread joins.
}

// The artifact test: the mutex makes every increment happen-before the next, so
// the total is exact and TSan stays silent. This runs in the normal suite.
TEST(w32, SafeCounterIsRaceFree) {
    cr::SafeCounter c;
    hammer(c);
    EXPECT_EQ(c.get(), static_cast<long>(kThreads) * kIters);
}

// The bug, quarantined behind DISABLED_ so ctest skips it and the gate stays
// green. Run it by hand (see header comment) to WATCH TSan catch the race. Two
// symptoms of one disease: the EXPECT_EQ below will also usually fail because
// lost updates make the count come out short of kThreads*kIters.
TEST(w32, DISABLED_RacyCounterRace) {
    cr::RacyCounter c;
    hammer(c);
    // Not a real assertion of correctness — this documents the SYMPTOM. Under a
    // race the value is indeterminate (UB); in practice it lands well below the
    // expected total. The point of running this is the TSan report, not this line.
    EXPECT_EQ(c.value, static_cast<long>(kThreads) * kIters)
        << "racy count came out short — lost updates from the data race";
}

}  // namespace
