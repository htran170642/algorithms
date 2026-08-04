#pragma once

// W34 — a BOUNDED BlockingQueue<T, Capacity> built on two counting_semaphores.
//
// This is the answer to the question W33 left open: an unbounded queue lets a
// fast producer grow memory without limit. A bounded queue applies BACK-PRESSURE
// — push() blocks when the queue is full — so RAM is capped at Capacity items.
//
// The core idea (C++20 std::counting_semaphore): two permit counters.
//   * slots_free_  starts at Capacity — a permit == "one empty slot you may fill".
//                  push() acquire()s one (BLOCKS when full); pop() release()s one.
//   * items_ready_ starts at 0        — a permit == "one item you may take".
//                  push() release()s one; pop() acquire()s one (BLOCKS when empty).
//
// Why a semaphore and not a condition_variable here? A semaphore REMEMBERS its
// signals (unlike a cv, which forgets a notify fired into the void — the W33
// lost-wakeup trap). release() before anyone waits still bumps the count, so the
// next acquire() succeeds immediately. That makes the producer/consumer counting
// almost impossible to get wrong.
//
// Shutdown is the one place semaphores are fiddlier than a cv's notify_all(). We
// use a RELAY: close() release()s items_ready_ once; the first consumer to wake
// to an empty+closed queue release()s it again before returning nullopt, passing
// the token to the next blocked consumer. One release() thus wakes them all.

#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <semaphore>
#include <utility>

namespace cr {

template <typename T, std::ptrdiff_t Capacity>
class BoundedQueue {
    static_assert(Capacity > 0, "BoundedQueue needs a positive capacity");

public:
    BoundedQueue() = default;

    // Rule of Zero with a reason (same as W33): std::mutex and std::semaphore are
    // neither copyable nor movable, so the compiler deletes copy/move for us. A
    // queue with in-flight waiters and a fixed capacity has no meaningful copy.
    BoundedQueue(const BoundedQueue&)            = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    // Block until a slot is free, then enqueue. Returns false if the queue was
    // closed (nothing enqueued). The slots_free_.acquire() IS the back-pressure:
    // a full queue parks the producer here at 0% CPU until a consumer frees a slot.
    bool push(T value) {
        slots_free_.acquire();                 // <-- back-pressure: blocks when full
        {
            std::lock_guard lk(m_);
            if (closed_) {
                slots_free_.release();          // hand the slot back; we didn't use it
                return false;
            }
            q_.push(std::move(value));
        }
        items_ready_.release();                 // signal: one more item to take
        return true;
    }

    // Block until an item is available, or the queue is closed and drained.
    // Returns nullopt only when closed AND empty = "nothing more will ever come".
    std::optional<T> pop() {
        items_ready_.acquire();                 // blocks when empty (or waits for close)
        std::unique_lock lk(m_);
        if (q_.empty()) {                       // woken by close() with nothing left
            lk.unlock();
            items_ready_.release();             // RELAY: wake the next blocked consumer
            return std::nullopt;
        }
        T value = std::move(q_.front());
        q_.pop();
        lk.unlock();
        slots_free_.release();                  // signal: one more empty slot to fill
        return value;
    }

    // Wake every blocked consumer and refuse further pushes. Releasing once is
    // enough: the relay in pop() propagates the wake down the line of waiters.
    // Expected usage (as in W33): call close() AFTER producers have finished.
    void close() {
        {
            std::lock_guard lk(m_);
            closed_ = true;
        }
        items_ready_.release();                 // one token; pop() relays it onward
    }

    [[nodiscard]] bool closed() const {
        std::lock_guard lk(m_);
        return closed_;
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lk(m_);
        return q_.size();
    }

private:
    std::queue<T>                     q_;
    mutable std::mutex                m_;                 // guards q_ and closed_
    std::counting_semaphore<Capacity> slots_free_{Capacity};  // empty slots (starts full)
    std::counting_semaphore<Capacity> items_ready_{0};        // ready items (starts empty)
    bool                              closed_ = false;
};

}  // namespace cr
