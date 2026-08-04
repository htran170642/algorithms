#include "phase05_concurrency/include/bounded_queue.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <chrono>
#include <latch>
#include <optional>
#include <semaphore>
#include <thread>
#include <vector>

// W34 — the three C++20 synchronization primitives. Every test below runs in the
// FULL matrix, tsan-20/-23 included. If back-pressure were broken (push not
// bounded), the capacity check would catch it; if the semaphore counting were
// wrong, conservation (sum in == sum out) would fail.

namespace {

// ---------------------------------------------------------------------------
// counting_semaphore, in a real component: BoundedQueue<T, Capacity>.
// ---------------------------------------------------------------------------

// Single thread: basic FIFO + closed semantics, no concurrency in play.
TEST(w34, BoundedFifoAndClose) {
    cr::BoundedQueue<int, 4> q;
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_EQ(q.size(), 2u);

    EXPECT_EQ(q.pop(), 1);                        // FIFO
    EXPECT_EQ(q.pop(), 2);

    q.close();
    EXPECT_FALSE(q.push(3));                       // push after close is rejected
    EXPECT_FALSE(q.pop().has_value());             // closed + empty -> nullopt, no block
}

// close() must wake a consumer already blocked in pop() and hand it nullopt.
// Without the relay (or notify), this test HANGS.
TEST(w34, CloseWakesBlockedConsumer) {
    cr::BoundedQueue<int, 4> q;
    std::jthread consumer([&] {
        EXPECT_FALSE(q.pop().has_value());         // blocks (empty), woken by close()
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    q.close();
    // consumer joins here; if close() failed to wake it, we'd deadlock.
}

// The real proof: many producers/consumers hammer a SMALL-capacity queue. Every
// item is conserved AND the queue never holds more than Capacity items at once
// (back-pressure actually bounds memory — the whole point over W33's queue).
TEST(w34, BoundedConservesItemsAndRespectsCapacity) {
    constexpr std::ptrdiff_t kCapacity    = 64;
    constexpr int            kProducers   = 4;
    constexpr int            kConsumers   = 4;
    constexpr int            kPerProducer = 50'000;
    constexpr long           kExpectedSum =
        static_cast<long>(kProducers) * kPerProducer * (kPerProducer - 1) / 2;

    cr::BoundedQueue<int, kCapacity> q;
    std::atomic<long> consumed_sum{0};
    std::atomic<int>  consumed_count{0};
    std::atomic<bool> capacity_violated{false};

    {
        std::vector<std::jthread> consumers;
        consumers.reserve(kConsumers);
        for (int c = 0; c < kConsumers; ++c) {
            consumers.emplace_back([&] {
                while (std::optional<int> v = q.pop()) {   // nullopt (closed+empty) ends it
                    consumed_sum.fetch_add(*v, std::memory_order_relaxed);
                    consumed_count.fetch_add(1, std::memory_order_relaxed);
                    if (q.size() > static_cast<std::size_t>(kCapacity)) {
                        capacity_violated.store(true, std::memory_order_relaxed);
                    }
                }
            });
        }
        {
            std::vector<std::jthread> producers;
            producers.reserve(kProducers);
            for (int p = 0; p < kProducers; ++p) {
                producers.emplace_back([&] {
                    for (int i = 0; i < kPerProducer; ++i) {
                        q.push(i);                 // blocks here whenever the queue is full
                    }
                });
            }
            // producers join here -> everything pushed.
        }
        q.close();                                 // wake consumers to drain then exit
        // consumers join here.
    }

    EXPECT_EQ(consumed_count.load(), kProducers * kPerProducer);
    EXPECT_EQ(consumed_sum.load(), kExpectedSum);  // conservation: sum in == sum out
    EXPECT_FALSE(capacity_violated.load());        // back-pressure held: never over Capacity
}

// ---------------------------------------------------------------------------
// std::latch — a one-shot countdown gate.
// ---------------------------------------------------------------------------

// A start-gate: no worker does real work until main opens the gate; they release
// together. And main waits on a done-latch for all workers to finish.
TEST(w34, LatchStartGateAndDoneSignal) {
    constexpr int kWorkers = 8;
    std::latch start_gate{1};          // opened once by main
    std::latch all_done{kWorkers};     // counts workers down to 0
    std::atomic<int> ran{0};

    std::vector<std::jthread> workers;
    workers.reserve(kWorkers);
    for (int i = 0; i < kWorkers; ++i) {
        workers.emplace_back([&] {
            start_gate.wait();                     // park until main opens the gate
            ran.fetch_add(1, std::memory_order_relaxed);
            all_done.count_down();                 // one-way: signal completion
        });
    }

    start_gate.count_down();                        // 1 -> 0: releases every waiter at once
    all_done.wait();                                // block until all kWorkers counted down
    EXPECT_EQ(ran.load(), kWorkers);
}

// ---------------------------------------------------------------------------
// std::barrier — a REUSABLE, phased rendezvous.
// ---------------------------------------------------------------------------

// Phased computation: each round every thread adds its id to a shared total, then
// meets at the barrier. The completion function (runs once, on one thread, before
// release) records the round number. Barrier resets for the next round -> reusable.
TEST(w34, BarrierPhasedRoundsWithCompletion) {
    constexpr int kThreads = 4;
    constexpr int kRounds  = 5;
    std::atomic<int> round{0};
    std::atomic<long> total{0};

    std::barrier sync{kThreads, [&] {              // completion fn: once per phase
        round.fetch_add(1, std::memory_order_relaxed);
    }};

    {
        std::vector<std::jthread> threads;
        threads.reserve(kThreads);
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&, id = t] {
                for (int r = 0; r < kRounds; ++r) {
                    total.fetch_add(id, std::memory_order_relaxed);
                    sync.arrive_and_wait();        // wait for all, then all proceed together
                }
            });
        }
        // threads join here.
    }

    // Each round contributes 0+1+2+3 = 6; over kRounds rounds:
    const long per_round = static_cast<long>(kThreads) * (kThreads - 1) / 2;
    EXPECT_EQ(total.load(), per_round * kRounds);
    EXPECT_EQ(round.load(), kRounds);              // completion fn fired exactly once per round
}

// ---------------------------------------------------------------------------
// std::counting_semaphore, standalone — a concurrency limiter (a pool of K
// permits). At most K threads may be inside the critical section at any instant.
// ---------------------------------------------------------------------------

TEST(w34, SemaphoreLimitsConcurrency) {
    constexpr int kPermits = 3;
    constexpr int kThreads = 16;
    std::counting_semaphore<kPermits> limiter{kPermits};
    std::atomic<int> inside{0};
    std::atomic<int> max_inside{0};

    {
        std::vector<std::jthread> threads;
        threads.reserve(kThreads);
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&] {
                limiter.acquire();                 // blocks if all kPermits are taken
                const int now = inside.fetch_add(1, std::memory_order_relaxed) + 1;
                int prev = max_inside.load(std::memory_order_relaxed);
                while (now > prev &&
                       !max_inside.compare_exchange_weak(prev, now,
                                                         std::memory_order_relaxed)) {
                }
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                inside.fetch_sub(1, std::memory_order_relaxed);
                limiter.release();
            });
        }
        // threads join here.
    }

    EXPECT_LE(max_inside.load(), kPermits);        // never more than kPermits concurrent
    EXPECT_GT(max_inside.load(), 0);
}

}  // namespace
