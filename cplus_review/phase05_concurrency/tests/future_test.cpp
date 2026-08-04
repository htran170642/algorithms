#include "phase05_concurrency/include/future.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>

// W35 — future / promise / packaged_task / async.
//
// The cr::Future/Promise tests prove our mini shared-state channel behaves like
// std::. The remaining tests exercise the REAL std:: facilities we deliberately
// did NOT clone — packaged_task, launch policies, the async blocking-dtor gotcha —
// because those are properties of async()/packaged_task, not of future itself.

namespace {

// ---------------------------------------------------------------------------
// cr::Future / cr::Promise — our own shared-state channel.
// ---------------------------------------------------------------------------

// The value travels from the producer thread to the consumer through get().
TEST(w35, PromiseDeliversValueAcrossThreads) {
    cr::Promise<int> p;
    cr::Future<int>  f = p.get_future();

    std::jthread producer([p = std::move(p)]() mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        p.set_value(42);                     // publish after the consumer is already blocked
    });

    EXPECT_EQ(f.get(), 42);                  // blocks until set_value, then consumes
    EXPECT_FALSE(f.valid());                 // one-shot: get() consumed the state
}

// An exception set on the write end is RETHROWN out of get() on the read end.
TEST(w35, ExceptionCrossesTheThreadBoundary) {
    cr::Promise<int> p;
    cr::Future<int>  f = p.get_future();

    std::jthread producer([p = std::move(p)]() mutable {
        try {
            throw std::runtime_error("boom");
        } catch (...) {
            p.set_exception(std::current_exception());   // ship the exception
        }
    });

    EXPECT_THROW(f.get(), std::runtime_error);           // rethrown here, in this thread
}

// get_future() twice and set_value() twice each mirror std:: by throwing.
TEST(w35, OneShotContract) {
    cr::Promise<int> p;
    (void)p.get_future();
    EXPECT_THROW((void)p.get_future(), std::future_error);   // future_already_retrieved

    p.set_value(1);
    EXPECT_THROW(p.set_value(2), std::future_error);         // promise_already_satisfied
}

// wait() blocks without consuming; a following get() still works.
TEST(w35, WaitThenGet) {
    cr::Promise<int> p;
    cr::Future<int>  f = p.get_future();
    p.set_value(7);

    f.wait();                                // does not consume
    EXPECT_TRUE(f.valid());
    EXPECT_EQ(f.get(), 7);
}

// ---------------------------------------------------------------------------
// std::packaged_task — a callable wired to a future. The building block of a
// ThreadPool (W36): move the task onto a queue, a worker runs it, caller get()s.
// ---------------------------------------------------------------------------

TEST(w35, PackagedTaskFeedsItsFuture) {
    std::packaged_task<int(int, int)> task([](int a, int b) { return a + b; });
    std::future<int>                  result = task.get_future();

    std::jthread worker(std::move(task), 20, 22);   // run it on another thread
    EXPECT_EQ(result.get(), 42);
}

// ---------------------------------------------------------------------------
// std::async — launch policies.
// ---------------------------------------------------------------------------

// launch::deferred is LAZY: nothing runs until get(). We prove it by checking the
// flag stays unset until we ask, and that it runs on the CALLING thread.
TEST(w35, DeferredRunsLazilyOnTheCallingThread) {
    std::atomic<bool> ran{false};
    const auto        caller = std::this_thread::get_id();
    std::thread::id   ran_on{};

    std::future<int> f = std::async(std::launch::deferred, [&] {
        ran.store(true);
        ran_on = std::this_thread::get_id();
        return 99;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(ran.load());                // still not run — deferred is lazy
    EXPECT_EQ(f.get(), 99);                  // NOW it runs
    EXPECT_TRUE(ran.load());
    EXPECT_EQ(ran_on, caller);               // deferred runs inline on the get() thread
}

// launch::async guarantees a separate thread of execution.
TEST(w35, AsyncRunsOnAnotherThread) {
    const auto                   caller = std::this_thread::get_id();
    std::future<std::thread::id> f =
        std::async(std::launch::async, [] { return std::this_thread::get_id(); });
    EXPECT_NE(f.get(), caller);
}

// The famous gotcha: a future from std::async, if not KEPT, blocks in its
// destructor until the task finishes. So a loop of un-stored async()s runs
// SEQUENTIALLY. We measure it: N tasks of ~20ms each take ~N*20ms, not ~20ms.
TEST(w35, UnstoredAsyncFutureBlocksInDtorSoLoopIsSerial) {
    constexpr int  kTasks = 4;
    constexpr auto kBusy  = std::chrono::milliseconds(20);
    std::atomic<int> done{0};

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kTasks; ++i) {
        // temporary future destroyed at the semicolon -> its dtor blocks here.
        // (void) discards the [[nodiscard]] future ON PURPOSE — that IS the gotcha.
        (void)std::async(std::launch::async, [&] {
            std::this_thread::sleep_for(kBusy);
            done.fetch_add(1, std::memory_order_relaxed);
        });
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(done.load(), kTasks);
    // Serial => at least kTasks*kBusy. Parallel would be ~kBusy. Use a safe margin.
    EXPECT_GE(elapsed, kTasks * kBusy - std::chrono::milliseconds(5));
}

}  // namespace
