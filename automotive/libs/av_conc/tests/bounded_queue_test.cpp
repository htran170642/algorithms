#include "av/conc/bounded_queue.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using av::conc::BoundedQueue;
using av::conc::PushStatus;

// --- Single-threaded behaviour ---------------------------------------------

TEST(Construction, RejectsZeroCapacity) {
    EXPECT_THROW(BoundedQueue<int>{0}, std::invalid_argument);
    EXPECT_NO_THROW(BoundedQueue<int>{1});
}

TEST(Ordering, IsFirstInFirstOut) {
    BoundedQueue<int> queue{4};
    for (int i = 0; i < 4; ++i) {
        ASSERT_EQ(queue.try_push(i), PushStatus::Ok);
    }
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(queue.pop().value(), i);
    }
}

TEST(Ordering, RingBufferWrapsAround) {
    // Push and pop more items than the capacity so head_ and tail_ both wrap.
    BoundedQueue<int> queue{3};
    for (int i = 0; i < 10; ++i) {
        ASSERT_EQ(queue.try_push(i), PushStatus::Ok) << "i=" << i;
        EXPECT_EQ(queue.pop().value(), i);
    }
    EXPECT_EQ(queue.size(), 0U);
}

TEST(Backpressure, TryPushReportsFullInsteadOfGrowing) {
    BoundedQueue<int> queue{2};
    EXPECT_EQ(queue.try_push(1), PushStatus::Ok);
    EXPECT_EQ(queue.try_push(2), PushStatus::Ok);
    EXPECT_EQ(queue.try_push(3), PushStatus::Full);
    EXPECT_EQ(queue.size(), 2U) << "a rejected push must not have been stored";

    // Making room lets the next one through.
    EXPECT_EQ(queue.pop().value(), 1);
    EXPECT_EQ(queue.try_push(3), PushStatus::Ok);
}

TEST(Backpressure, TryPopReportsEmptyWithoutBlocking) {
    BoundedQueue<int> queue{2};
    EXPECT_FALSE(queue.try_pop().has_value());
    ASSERT_EQ(queue.try_push(7), PushStatus::Ok);
    EXPECT_EQ(queue.try_pop().value(), 7);
    EXPECT_FALSE(queue.try_pop().has_value());
}

// --- Close semantics -------------------------------------------------------

TEST(Close, DrainsBeforeReportingEmpty) {
    // The whole point of a graceful shutdown: closing stops new work from
    // arriving, it does not throw away work already queued.
    BoundedQueue<int> queue{4};
    ASSERT_EQ(queue.try_push(1), PushStatus::Ok);
    ASSERT_EQ(queue.try_push(2), PushStatus::Ok);

    queue.close();

    EXPECT_EQ(queue.pop().value(), 1);
    EXPECT_EQ(queue.pop().value(), 2);
    EXPECT_FALSE(queue.pop().has_value()) << "only now, once drained, does pop give up";
}

TEST(Close, RejectsFurtherPushes) {
    BoundedQueue<int> queue{4};
    queue.close();
    EXPECT_EQ(queue.try_push(1), PushStatus::Closed);
    EXPECT_EQ(queue.push(1), PushStatus::Closed);
    EXPECT_EQ(queue.size(), 0U);
}

TEST(Close, IsIdempotent) {
    BoundedQueue<int> queue{4};
    queue.close();
    queue.close();
    EXPECT_TRUE(queue.closed());
    EXPECT_FALSE(queue.pop().has_value());
}

TEST(Close, WakesABlockedPop) {
    BoundedQueue<int> queue{4};
    std::atomic<bool> returned{false};

    std::thread consumer([&] {
        const auto value = queue.pop();  // blocks: queue is empty and open
        EXPECT_FALSE(value.has_value());
        returned.store(true);
    });

    queue.close();
    consumer.join();  // hangs forever if close() failed to wake the waiter
    EXPECT_TRUE(returned.load());
}

TEST(Close, WakesABlockedPush) {
    BoundedQueue<int> queue{1};
    ASSERT_EQ(queue.try_push(1), PushStatus::Ok);  // now full

    std::atomic<bool> returned{false};
    std::thread producer([&] {
        EXPECT_EQ(queue.push(2), PushStatus::Closed);  // blocks: queue is full
        returned.store(true);
    });

    queue.close();
    producer.join();
    EXPECT_TRUE(returned.load());
}

TEST(Close, LetsAConsumerLoopTerminateOnItsOwn) {
    // This is the shutdown idiom the cockpit app uses: no stop flag, no
    // timeout, no polling -- the loop ends because the queue says so.
    BoundedQueue<int> queue{8};
    std::vector<int> seen;

    std::thread consumer([&] {
        while (const auto value = queue.pop()) {
            seen.push_back(*value);
        }
    });

    for (int i = 0; i < 100; ++i) {
        ASSERT_EQ(queue.push(i), PushStatus::Ok);
    }
    queue.close();
    consumer.join();

    ASSERT_EQ(seen.size(), 100U) << "close() must not discard queued work";
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(seen[static_cast<std::size_t>(i)], i);
    }
}

// --- Blocking actually blocks ----------------------------------------------

TEST(Blocking, PushWaitsForRoomInsteadOfOverflowing) {
    BoundedQueue<int> queue{2};
    ASSERT_EQ(queue.try_push(1), PushStatus::Ok);
    ASSERT_EQ(queue.try_push(2), PushStatus::Ok);

    // If push() did not block, the queue would exceed its capacity.
    std::thread producer([&] { EXPECT_EQ(queue.push(3), PushStatus::Ok); });

    EXPECT_EQ(queue.pop().value(), 1);
    producer.join();

    EXPECT_LE(queue.size(), queue.capacity());
    EXPECT_EQ(queue.pop().value(), 2);
    EXPECT_EQ(queue.pop().value(), 3);
}

TEST(Blocking, PopWaitsForAnItem) {
    BoundedQueue<int> queue{2};
    std::thread consumer([&] { EXPECT_EQ(queue.pop().value(), 42); });
    ASSERT_EQ(queue.push(42), PushStatus::Ok);
    consumer.join();
}

// --- Concurrency (the reason this file runs under TSan) --------------------

/// Asserts that the consumers between them saw every value in [0, total) once.
void expect_exactly_once(const std::vector<std::vector<int>>& received, int total) {
    std::set<int> all;
    std::size_t count = 0;
    for (const auto& batch : received) {
        count += batch.size();
        all.insert(batch.begin(), batch.end());
    }
    EXPECT_EQ(count, static_cast<std::size_t>(total)) << "an item was lost or delivered twice";
    ASSERT_EQ(all.size(), static_cast<std::size_t>(total));
    EXPECT_EQ(*all.begin(), 0);
    EXPECT_EQ(*all.rbegin(), total - 1);
}

/// std::atomic has no fetch_max before C++26, so this is the CAS loop by hand.
void record_max(std::atomic<std::size_t>& worst, std::size_t observed) {
    std::size_t previous = worst.load();
    while (observed > previous && !worst.compare_exchange_weak(previous, observed)) {
        // compare_exchange_weak refreshes `previous` on failure.
    }
}

TEST(Concurrency, EveryItemArrivesExactlyOnce) {
    constexpr int kProducers = 4;
    constexpr int kConsumers = 3;
    constexpr int kPerProducer = 500;
    constexpr int kTotal = kProducers * kPerProducer;

    BoundedQueue<int> queue{16};  // deliberately smaller than the work
    std::vector<std::vector<int>> received(kConsumers);

    std::vector<std::thread> consumers;
    consumers.reserve(kConsumers);
    for (int c = 0; c < kConsumers; ++c) {
        consumers.emplace_back([&, c] {
            while (const auto value = queue.pop()) {
                received[static_cast<std::size_t>(c)].push_back(*value);
            }
        });
    }

    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p] {
            for (int i = 0; i < kPerProducer; ++i) {
                ASSERT_EQ(queue.push((p * kPerProducer) + i), PushStatus::Ok);
            }
        });
    }

    for (std::thread& producer : producers) {
        producer.join();
    }
    queue.close();  // only after every producer is done
    for (std::thread& consumer : consumers) {
        consumer.join();
    }

    // Nothing lost, nothing duplicated: the union of what the consumers saw is
    // exactly the set of values that were pushed.
    expect_exactly_once(received, kTotal);
}

TEST(Concurrency, CapacityIsNeverExceeded) {
    constexpr std::size_t kCapacity = 4;
    BoundedQueue<int> queue{kCapacity};
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> worst{0};

    std::thread watcher([&] {
        while (!stop.load()) {
            record_max(worst, queue.size());
        }
    });

    std::thread consumer([&] {
        while (queue.pop()) {
        }
    });

    for (int i = 0; i < 5000; ++i) {
        ASSERT_EQ(queue.push(i), PushStatus::Ok);
    }
    queue.close();
    consumer.join();
    stop.store(true);
    watcher.join();

    EXPECT_LE(worst.load(), kCapacity);
}

TEST(Concurrency, TryPushUnderContentionNeverStoresMoreThanCapacity) {
    constexpr std::size_t kCapacity = 8;
    BoundedQueue<int> queue{kCapacity};
    std::atomic<std::uint64_t> accepted{0};
    std::atomic<std::uint64_t> rejected{0};

    std::vector<std::thread> producers;
    producers.reserve(4);
    for (int p = 0; p < 4; ++p) {
        producers.emplace_back([&] {
            for (int i = 0; i < 1000; ++i) {
                if (queue.try_push(i) == PushStatus::Ok) {
                    accepted.fetch_add(1);
                } else {
                    rejected.fetch_add(1);
                }
            }
        });
    }
    for (std::thread& producer : producers) {
        producer.join();
    }

    EXPECT_EQ(accepted.load() + rejected.load(), 4000U);
    EXPECT_LE(queue.size(), kCapacity);
    EXPECT_GT(rejected.load(), 0U) << "with no consumer, a bounded queue must reject";
}

}  // namespace
