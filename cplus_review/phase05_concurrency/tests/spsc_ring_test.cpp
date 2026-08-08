#include "phase05_concurrency/include/spsc_ring.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <thread>

// W38 — SPSC lock-free ring buffer.
//
// The single-threaded tests pin down the FIFO contract and the full/empty edges.
// The threaded test is the real one: one producer, one consumer, no lock — it MUST
// stay green under tsan-20/-23. If the release/acquire edges were wrong, TSan would
// report a data race on the payload (a plain-old value published across threads).
//   cmake --preset tsan-23 && cmake --build --preset tsan-23
//   ctest --preset tsan-23 --output-on-failure

namespace {

TEST(w38, PushPopIsFifo) {
    cr::SpscRing<int, 4> ring;
    EXPECT_TRUE(ring.empty());
    EXPECT_TRUE(ring.push(1));
    EXPECT_TRUE(ring.push(2));
    EXPECT_TRUE(ring.push(3));
    EXPECT_EQ(ring.size(), 3u);

    int v = 0;
    EXPECT_TRUE(ring.pop(v)); EXPECT_EQ(v, 1);
    EXPECT_TRUE(ring.pop(v)); EXPECT_EQ(v, 2);
    EXPECT_TRUE(ring.pop(v)); EXPECT_EQ(v, 3);
    EXPECT_TRUE(ring.empty());
}

TEST(w38, PushFailsWhenFull) {
    cr::SpscRing<int, 4> ring;   // all 4 slots usable (monotonic indices, no wasted slot)
    EXPECT_TRUE(ring.push(10));
    EXPECT_TRUE(ring.push(20));
    EXPECT_TRUE(ring.push(30));
    EXPECT_TRUE(ring.push(40));
    EXPECT_EQ(ring.size(), ring.capacity());
    EXPECT_FALSE(ring.push(50));   // full
}

TEST(w38, PopFailsWhenEmpty) {
    cr::SpscRing<int, 4> ring;
    int v = -1;
    EXPECT_FALSE(ring.pop(v));
    EXPECT_EQ(v, -1);   // out untouched on failure
}

TEST(w38, WrapAroundReusesSlots) {
    cr::SpscRing<int, 2> ring;
    int v = 0;
    for (int i = 0; i < 1000; ++i) {   // far more than capacity: indices wrap via mask
        EXPECT_TRUE(ring.push(i));
        EXPECT_TRUE(ring.pop(v));
        EXPECT_EQ(v, i);
    }
    EXPECT_TRUE(ring.empty());
}

// Move-only payload: proves emplace/pop use placement-new + move, never a copy, and
// the destructor cleans up in-flight elements (leak-checked under ASan).
TEST(w38, SupportsMoveOnlyType) {
    cr::SpscRing<std::unique_ptr<int>, 4> ring;
    EXPECT_TRUE(ring.push(std::make_unique<int>(7)));
    EXPECT_TRUE(ring.push(std::make_unique<int>(8)));
    std::unique_ptr<int> out;
    EXPECT_TRUE(ring.pop(out));
    ASSERT_TRUE(out);
    EXPECT_EQ(*out, 7);
    // Leave one element in the ring: the destructor must free it (no ASan leak).
}

// The point of the whole week: producer and consumer run concurrently with no lock.
// Producer pushes 0..N-1 (spinning while full); consumer pops N values (spinning while
// empty) and checks they arrive in order and sum correctly. Race-free by construction,
// so TSan stays silent.
TEST(w38, SingleProducerSingleConsumerStress) {
    constexpr std::int64_t kN = 1'000'000;
    cr::SpscRing<std::int64_t, 1024> ring;

    std::int64_t consumed_sum   = 0;
    std::int64_t consumed_count = 0;
    bool         in_order       = true;

    std::thread consumer([&] {
        std::int64_t expected = 0;
        std::int64_t v        = 0;
        while (consumed_count < kN) {
            if (ring.pop(v)) {
                if (v != expected) in_order = false;
                consumed_sum += v;
                ++expected;
                ++consumed_count;
            } else {
                std::this_thread::yield();   // empty: back off
            }
        }
    });

    std::thread producer([&] {
        for (std::int64_t i = 0; i < kN; ++i) {
            while (!ring.push(i)) {
                std::this_thread::yield();   // full: back off
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_TRUE(in_order) << "SPSC must preserve FIFO order across threads";
    EXPECT_EQ(consumed_count, kN);
    EXPECT_EQ(consumed_sum, (kN - 1) * kN / 2);   // 0 + 1 + ... + (N-1)
}

}  // namespace
