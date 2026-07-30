#include "phase05_concurrency/include/blocking_queue.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <vector>

// W33 — BlockingQueue. These run in the FULL matrix, including tsan-20/-23:
//   cmake --preset tsan-23 && cmake --build --preset tsan-23
//   ctest --preset tsan-23 --output-on-failure
// If push weren't done under the lock, TSan would flag q_ (and the stress test
// would occasionally hang on a lost wakeup). Green under TSan is the proof.

namespace {

// Single thread: basic FIFO + try_pop semantics, no concurrency in play.
TEST(w33, FifoOrderAndTryPop) {
    cr::BlockingQueue<int> q;
    EXPECT_FALSE(q.try_pop().has_value());      // empty -> nullopt immediately

    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_EQ(q.size(), 2u);

    EXPECT_EQ(q.pop(), 1);                       // FIFO
    EXPECT_EQ(q.pop(), 2);
    EXPECT_FALSE(q.try_pop().has_value());
}

// close() must wake a consumer already blocked in pop() and hand it nullopt.
// Without notify_all in close(), this test HANGS — that's the lost-wakeup-on-
// shutdown bug made visible.
TEST(w33, CloseWakesBlockedConsumer) {
    cr::BlockingQueue<int> q;

    std::jthread consumer([&] {
        std::optional<int> v = q.pop();          // blocks: queue empty, not closed
        EXPECT_FALSE(v.has_value());             // woken by close() -> nullopt
    });

    // Let the consumer reach wait(), then close. Even if close() runs first, the
    // predicate (empty || closed_) already holds, so pop() returns without blocking.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    q.close();
    // consumer's jthread joins here; if close() failed to wake it, we'd deadlock.
}

// A push AFTER close() is rejected and does not enqueue.
TEST(w33, PushAfterCloseRejected) {
    cr::BlockingQueue<int> q;
    q.close();
    EXPECT_FALSE(q.push(42));
    EXPECT_EQ(q.size(), 0u);
    EXPECT_FALSE(q.pop().has_value());           // closed + empty -> nullopt, no block
}

// The real test: N producers, M consumers, hammer the queue, then close and
// drain. Every produced item is consumed exactly once (no lost updates), nothing
// is invented, and no consumer is left blocked (no lost wakeup). Clean under TSan.
TEST(w33, ManyProducersManyConsumersConserveItems) {
    constexpr int  kProducers   = 4;
    constexpr int  kConsumers   = 4;
    constexpr int  kPerProducer = 50'000;
    constexpr long kExpectedSum =
        static_cast<long>(kProducers) * kPerProducer * (kPerProducer - 1) / 2;

    cr::BlockingQueue<int> q;
    std::atomic<long> consumed_sum{0};
    std::atomic<int>  consumed_count{0};

    {
        std::vector<std::jthread> consumers;
        consumers.reserve(kConsumers);
        for (int c = 0; c < kConsumers; ++c) {
            consumers.emplace_back([&] {
                while (std::optional<int> v = q.pop()) {   // nullopt (closed+empty) ends the loop
                    consumed_sum.fetch_add(*v, std::memory_order_relaxed);
                    consumed_count.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        {
            std::vector<std::jthread> producers;
            producers.reserve(kProducers);
            for (int p = 0; p < kProducers; ++p) {
                producers.emplace_back([&] {
                    for (int i = 0; i < kPerProducer; ++i) {
                        q.push(i);
                    }
                });
            }
            // producers join here -> everything has been pushed.
        }
        q.close();                                 // now safe to close; consumers drain then exit
        // consumers join here.
    }

    EXPECT_EQ(consumed_count.load(), kProducers * kPerProducer);
    EXPECT_EQ(consumed_sum.load(), kExpectedSum);  // conservation: sum in == sum out
}

}  // namespace
