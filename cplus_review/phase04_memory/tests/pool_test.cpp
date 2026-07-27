// W30 — proving the pool recycles slots and runs lifetimes correctly.

#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "phase01_modern_cpp/include/probe.hpp"   // cr::Probe, reused from W1
#include "w30/pool.hpp"

namespace {

using cr::Probe;

TEST(Pool, CreateRunsCtorDestroyRunsDtor) {
    Probe::setVerbose(false);
    Probe::reset();

    w30::ObjectPool<Probe> pool;
    Probe* p = pool.create("pooled");
    EXPECT_EQ(Probe::counters().value_ctor, 1);
    EXPECT_EQ(Probe::counters().dtor, 0);
    EXPECT_EQ(p->name(), "pooled");

    pool.destroy(p);
    EXPECT_EQ(Probe::counters().dtor, 1);
}

// The defining property: a freed slot is REUSED, not returned to the OS. Free
// one object, create another, and the pool hands back the same address (LIFO).
TEST(Pool, FreedSlotIsRecycled) {
    Probe::setVerbose(false);
    Probe::reset();

    w30::ObjectPool<Probe> pool;
    Probe* a = pool.create("a");
    void*  addr_a = a;
    pool.destroy(a);

    Probe* b = pool.create("b");
    EXPECT_EQ(static_cast<void*>(b), addr_a);   // same slot, recycled
    pool.destroy(b);

    EXPECT_EQ(Probe::counters().value_ctor, 2);
    EXPECT_EQ(Probe::counters().dtor, 2);        // two full, balanced lifetimes
}

// Allocate past one chunk (64) to force grow(); every pointer must stay valid
// and distinct while live — chunks are never moved or freed mid-flight.
TEST(Pool, GrowsAcrossChunksAndKeepsPointersValid) {
    Probe::setVerbose(false);
    Probe::reset();

    constexpr int kN = 200;                       // > default 64 per chunk
    w30::ObjectPool<Probe> pool{/*blocks_per_chunk=*/64};

    std::vector<Probe*> live;
    live.reserve(kN);
    for (int i = 0; i < kN; ++i) {
        live.push_back(pool.create(std::to_string(i)));
    }
    EXPECT_EQ(Probe::counters().value_ctor, kN);

    // All live pointers are distinct (no slot handed out twice).
    for (int i = 0; i < kN; ++i) {
        EXPECT_EQ(live[static_cast<std::size_t>(i)]->name(), std::to_string(i));
    }

    for (Probe* p : live) {
        pool.destroy(p);
    }
    EXPECT_EQ(Probe::counters().dtor, kN);
}

}  // namespace
