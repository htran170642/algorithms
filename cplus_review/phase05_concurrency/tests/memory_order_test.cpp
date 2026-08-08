#include "phase05_concurrency/include/memory_order_litmus.hpp"

#include <gtest/gtest.h>

#include <iostream>

// W37 — Atomics + memory_order. Four assertions, one per tool.
//
// The whole suite must stay green under tsan-20/-23: every demo is race-free by
// construction (that is the entire point — memory_order is about VISIBLE ORDER,
// not about avoiding a data race). TSan should never fire here.
//   cmake --preset tsan-23 && cmake --build --preset tsan-23
//   ctest --preset tsan-23 --output-on-failure

namespace {

// The invariant we CAN assert without flakiness: under seq_cst, the Store-Buffer
// outcome r1==0 && r2==0 is impossible — there is a single total order over all
// seq_cst operations, and in it one store precedes the other's load. So zero_zero
// MUST be 0, on every run, on every platform.
TEST(w37, SeqCstForbidsStoreBufferReorder) {
    const cr::SbResult sc = cr::store_buffer_litmus(/*seq_cst=*/true, /*rounds=*/20'000);
    EXPECT_EQ(sc.zero_zero, 0)
        << "seq_cst must forbid the (0,0) outcome — a total order exists over all "
           "seq_cst ops; if this fires the model or the compiler is broken";
}

// The teaching half: under relaxed, x86 exposes StoreLoad reordering, so (0,0)
// CAN happen. We do NOT assert it is > 0 — that would be a flaky test (a run may
// legally observe zero, and a strongly-ordered platform never reorders). We print
// it so the learner SEES the difference against the seq_cst run above.
TEST(w37, RelaxedCanReorder_Observed) {
    const cr::SbResult rx = cr::store_buffer_litmus(/*seq_cst=*/false, /*rounds=*/20'000);
    std::cout << "[ w37 ] relaxed Store-Buffer: " << rx.zero_zero << " / " << rx.rounds
              << " rounds observed r1==0 && r2==0 (seq_cst forbids all of them)\n";
    SUCCEED();   // observation, not an assertion — see comment above
}

// Release-store publishes the prior plain write; the acquire-load that reads the
// flag sees it. Never 0.
TEST(w37, ReleaseAcquirePublishesPayload) {
    EXPECT_EQ(cr::release_acquire_message(), 42);
}

// Same happens-before edge, but the consumer parked instead of spinning.
TEST(w37, WaitNotifyKeepsTheSameSynchronization) {
    EXPECT_EQ(cr::wait_notify_message(), 42);
}

// atomic_ref borrows atomicity for each plain vector slot: no lost updates.
TEST(w37, AtomicRefSumIsExact) {
    constexpr int kThreads   = 8;
    constexpr int kPerThread = 100'000;
    EXPECT_EQ(cr::atomic_ref_sum(kThreads, kPerThread),
              static_cast<long>(kThreads) * kPerThread);
}

}  // namespace
