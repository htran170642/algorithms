#include "phase05_concurrency/include/thread_pool.hpp"
#include "phase05_concurrency/include/thread_pool_mo.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

// W36 — ThreadPool contract, run against BOTH implementations via a typed test:
//   * Lối A  cr::ThreadPool    — queue of std::function<void()>; the move-only
//                               packaged_task is made copyable behind a shared_ptr.
//   * Lối B  cr::ThreadPoolMO  — queue of a MOVE-ONLY element
//                               (std::move_only_function on C++23, packaged_task
//                               fallback on C++20); no shared_ptr.
//
// One suite, two types: if both pass every case, they satisfy the SAME public
// contract and differ only in the mechanics we deliberately chose to compare.
// Interface both must honor:
//
//   Pool pool;                                   // n = hardware_concurrency()
//   Pool pool(4);                                // explicit worker count
//   std::future<R> f = pool.submit(f, args...);  // R = invoke_result_t<F, Args...>
//   std::size_t    n = pool.worker_count();
//   // dtor: graceful — runs everything already queued, then joins.

namespace {

template <typename Pool>
class ThreadPoolTest : public ::testing::Test {};

using PoolTypes = ::testing::Types<cr::ThreadPool, cr::ThreadPoolMO>;
TYPED_TEST_SUITE(ThreadPoolTest, PoolTypes);

// A submitted task's return value comes back through the future.
TYPED_TEST(ThreadPoolTest, RunsTaskAndReturnsValue) {
    TypeParam pool(2);
    std::future<int> f = pool.submit([] { return 42; });
    EXPECT_EQ(f.get(), 42);
}

// submit forwards arguments into the callable.
TYPED_TEST(ThreadPoolTest, ForwardsArgumentsToTheCallable) {
    TypeParam pool(2);
    std::future<int> f = pool.submit([](int a, int b) { return a + b; }, 20, 22);
    EXPECT_EQ(f.get(), 42);
}

// An exception thrown inside the task is rethrown out of future.get() — the
// packaged_task shared state carries it across the worker->caller boundary.
TYPED_TEST(ThreadPoolTest, ExceptionPropagatesThroughTheFuture) {
    TypeParam pool(2);
    std::future<void> f = pool.submit([] { throw std::runtime_error("boom"); });
    EXPECT_THROW(f.get(), std::runtime_error);
}

// Every submitted task must run exactly once — collect all results and sum them.
TYPED_TEST(ThreadPoolTest, RunsEverySubmittedTaskExactlyOnce) {
    TypeParam pool(4);
    constexpr int kN = 1000;

    std::vector<std::future<int>> results;
    results.reserve(kN);
    for (int i = 0; i < kN; ++i) {
        results.push_back(pool.submit([i] { return i; }));
    }

    long long sum = 0;
    for (auto& f : results) {
        sum += f.get();
    }
    EXPECT_EQ(sum, static_cast<long long>(kN) * (kN - 1) / 2);  // 0+1+...+(N-1)
}

// With >=2 workers, N blocking tasks finish in well under N*duration: proof the
// pool actually runs them concurrently rather than one-at-a-time.
TYPED_TEST(ThreadPoolTest, ActuallyRunsTasksInParallel) {
    TypeParam pool;
    const std::size_t n = pool.worker_count();
    if (n < 2) {
        GTEST_SKIP() << "single-core box: cannot observe parallelism";
    }

    constexpr auto kBusy = std::chrono::milliseconds(40);
    std::vector<std::future<void>> fs;
    for (std::size_t i = 0; i < n; ++i) {
        fs.push_back(pool.submit([kBusy] { std::this_thread::sleep_for(kBusy); }));
    }

    const auto start = std::chrono::steady_clock::now();
    for (auto& f : fs) {
        f.get();
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;

    // Serial would be n*kBusy. Parallel is ~kBusy. Allow a fat margin for slow
    // sanitizer runs — we only need to distinguish "parallel" from "serial".
    EXPECT_LT(elapsed, n * kBusy / 2);
}

// Graceful shutdown: the destructor drains work already in the queue before it
// joins. counter lives past the pool so we can read it after the pool is gone.
TYPED_TEST(ThreadPoolTest, DestructorDrainsQueuedTasks) {
    std::atomic<int> counter{0};
    constexpr int    kN = 200;
    {
        TypeParam pool(2);
        for (int i = 0; i < kN; ++i) {
            pool.submit([&counter] {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }
    }  // ~Pool: must run all kN queued tasks, THEN join.
    EXPECT_EQ(counter.load(), kN);
}

// The pool always has at least one worker (hardware_concurrency() can report 0).
TYPED_TEST(ThreadPoolTest, WorkerCountIsAlwaysPositive) {
    TypeParam pool;
    EXPECT_GE(pool.worker_count(), 1u);

    TypeParam sized(3);
    EXPECT_EQ(sized.worker_count(), 3u);
}

}  // namespace
