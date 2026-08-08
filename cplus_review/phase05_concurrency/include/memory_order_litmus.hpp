#pragma once

// W37 — Atomics + memory_order, packaged as callable demos the tests can assert on.
//
// Four things a Senior must be able to *show*, not just recite:
//
//   1. store_buffer_litmus  — the ONE reordering x86 hardware actually does
//        (StoreLoad). Two threads each store their own flag then read the other's.
//        Under relaxed, BOTH can read 0 — a global order in which neither store is
//        visible to the other's load. Under seq_cst, that outcome is forbidden.
//        This is the litmus that proves "memory_order is about VISIBLE ORDER, not
//        about protecting one variable" — the atomics are race-free either way.
//
//   2. release_acquire_message — message passing. A release-store publishes every
//        prior write; a matching acquire-load that reads the flag sees them all.
//        The payload `data` is a PLAIN int and still race-free: the release/acquire
//        pair is the happens-before edge.
//
//   3. wait_notify_message — the SAME release/acquire synchronization, but the
//        consumer parks (0% CPU) via atomic::wait instead of spinning. wait/notify
//        changes HOW you wait, not HOW you synchronize.
//
//   4. atomic_ref_sum — borrow atomicity for elements of a PLAIN vector<int>
//        (you cannot build vector<atomic<int>>: atomic isn't copyable). The view
//        is atomic only while the atomic_ref is alive.
//
// Throwaway teaching glue: real code reaches for std::atomic<T> directly. The value
// here is the litmus counters and the assertions, not a reusable component.

#include <atomic>
#include <barrier>
#include <cstddef>
#include <numeric>
#include <thread>
#include <vector>

namespace cr {

struct SbResult {
    int rounds     = 0;   // how many rounds we ran
    int zero_zero  = 0;   // rounds that observed r1 == 0 && r2 == 0
};

// Store-Buffer litmus. Two persistent worker threads run in lockstep via a
// 3-party barrier (both workers + main), so their store/load windows actually
// overlap — per-iteration fresh threads almost never do, and you'd never see the
// reorder. `seq_cst == false` uses relaxed (x86 will expose StoreLoad → some
// 0/0); `seq_cst == true` uses seq_cst (0/0 becomes impossible).
[[nodiscard]] inline SbResult store_buffer_litmus(bool seq_cst, int rounds) {
    const auto ord = seq_cst ? std::memory_order_seq_cst : std::memory_order_relaxed;

    std::atomic<int> x{0};
    std::atomic<int> y{0};
    std::vector<int> r1(static_cast<std::size_t>(rounds), -1);
    std::vector<int> r2(static_cast<std::size_t>(rounds), -1);

    std::barrier sync(3);   // both workers + main meet twice per round

    std::jthread t0([&] {
        for (int k = 0; k < rounds; ++k) {
            sync.arrive_and_wait();                       // main has reset x,y
            x.store(1, ord);
            r1[static_cast<std::size_t>(k)] = y.load(ord);
            sync.arrive_and_wait();                       // let main read + reset
        }
    });
    std::jthread t1([&] {
        for (int k = 0; k < rounds; ++k) {
            sync.arrive_and_wait();
            y.store(1, ord);
            r2[static_cast<std::size_t>(k)] = x.load(ord);
            sync.arrive_and_wait();
        }
    });

    SbResult out{rounds, 0};
    for (int k = 0; k < rounds; ++k) {
        x.store(0, std::memory_order_relaxed);            // reset BEFORE the round
        y.store(0, std::memory_order_relaxed);
        sync.arrive_and_wait();                           // release workers
        sync.arrive_and_wait();                           // wait for their results
        if (r1[static_cast<std::size_t>(k)] == 0 && r2[static_cast<std::size_t>(k)] == 0)
            ++out.zero_zero;
    }
    return out;   // jthreads join here
}

// Message passing via a release-store / acquire-load pair. Returns the payload the
// consumer observed — must be 42, never 0.
[[nodiscard]] inline int release_acquire_message() {
    std::atomic<bool> ready{false};
    int               data = 0;   // plain int, published by the flag

    std::jthread producer([&] {
        data = 42;                                          // (1) prior write
        ready.store(true, std::memory_order_release);       // (2) publishes (1)
    });

    while (!ready.load(std::memory_order_acquire)) {        // (3) acquire: sees (1)
        std::this_thread::yield();
    }
    return data;
}

// Same synchronization, but the consumer PARKS instead of spinning.
[[nodiscard]] inline int wait_notify_message() {
    std::atomic<bool> ready{false};
    int               data = 0;

    std::jthread producer([&] {
        data = 42;                                          // (1)
        ready.store(true, std::memory_order_release);       // (2) release
        ready.notify_one();                                 // (3) wake the parked consumer
    });

    ready.wait(false, std::memory_order_acquire);           // sleep while == false; acquire-load
    return data;
}

// atomic_ref over a plain vector<int>: `threads` workers each fetch_add `per_thread`
// times into their OWN slot. Returns the grand total (== threads * per_thread).
[[nodiscard]] inline long atomic_ref_sum(int threads, int per_thread) {
    std::vector<int> counters(static_cast<std::size_t>(threads), 0);
    {
        std::vector<std::jthread> workers;
        workers.reserve(static_cast<std::size_t>(threads));
        for (int i = 0; i < threads; ++i) {
            workers.emplace_back([&counters, i, per_thread] {
                std::atomic_ref<int> slot{counters[static_cast<std::size_t>(i)]};
                for (int n = 0; n < per_thread; ++n)
                    slot.fetch_add(1, std::memory_order_relaxed);
            });
        }
    }   // join: every atomic_ref is dead, counters is plain int[] again
    return std::reduce(counters.begin(), counters.end(), 0L);
}

}  // namespace cr
