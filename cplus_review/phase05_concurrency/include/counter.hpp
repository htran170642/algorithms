#pragma once

// W32 — two counters that differ by ONE thing: synchronization.
//
//   RacyCounter : ++value from many threads with NO happens-before between them.
//                 A data race = UNDEFINED BEHAVIOR (not "wrong number" — UB).
//                 TSan is the only reliable way to *see* it, because the bug is
//                 in the memory model, not in any single observable value.
//   SafeCounter : the same increment, serialized through a mutex. Every access —
//                 the read too — goes through the SAME lock, so there is a
//                 happens-before edge between any two operations. No race.
//
// Thrown away after this week: in real code you'd reach for std::atomic<long>
// (W37) for a plain counter — a mutex here is deliberately the teaching vehicle
// for lock_guard / scoped_lock / unique_lock.

#include <mutex>

namespace cr {

// The bug on purpose. `inc()` is a read-modify-write (`++value`) with zero
// synchronization; call it from two threads and you have a data race.
struct RacyCounter {
    long value = 0;
    void inc() noexcept { ++value; }   // <-- TSan will point its two stacks here
};

// The fix. Same logic, but every touch of `value_` is serialized by `m_`.
class SafeCounter {
public:
    void inc() {
        // lock_guard: the C++11 workhorse for "one mutex, whole scope".
        // scoped_lock (C++17) would be the modern default and is required when
        // you must hold TWO mutexes at once — here, with a single mutex, either
        // is correct; lock_guard keeps the intent minimal.
        std::lock_guard<std::mutex> lk(m_);
        ++value_;
    }

    // Reads must lock too. This is the trap from quiz Q5: synchronization has to
    // be SYMMETRIC. An unlocked read racing a locked write is still a data race —
    // there is no happens-before between them. Locking here creates that edge.
    [[nodiscard]] long get() const {
        std::lock_guard<std::mutex> lk(m_);
        return value_;
    }

private:
    long               value_ = 0;
    mutable std::mutex m_;   // mutable: get() is const but must still lock
};

}  // namespace cr
