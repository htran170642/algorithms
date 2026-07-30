#pragma once

// W33 — a bounded-less BlockingQueue<T>: the canonical condition_variable pattern.
//
// This is a REAL component (the standard has no equivalent), unlike W32's
// throwaway counter. It reappears in W36 (ThreadPool) and W56 (producer-consumer).
//
// Two bugs this design exists to NOT have:
//   * lost wakeup  — the state change (push) happens WHILE HOLDING the mutex, so
//                    a notify can never slip into the gap between a consumer
//                    checking the predicate and actually blocking in wait().
//   * spurious wakeup — every wait() re-checks a predicate (the wait(lock, pred)
//                    overload IS the `while (!pred) wait(lock);` loop), so a
//                    wake with no work just goes back to sleep.
//
// Shutdown: close() flips closed_ (under the lock) and notify_all()s so every
// blocked consumer wakes; a consumer that wakes to an empty+closed queue returns
// nullopt = "no more work, ever".

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

namespace cr {

template <typename T>
class BlockingQueue {
public:
    BlockingQueue() = default;

    // Rule of Zero, and it's not laziness: std::mutex and std::condition_variable
    // are neither copyable nor movable, so the compiler DELETES this type's copy
    // and move members for us. A queue with in-flight waiters has no meaningful
    // copy or move anyway — you share it by reference (or shared_ptr), never by value.
    BlockingQueue(const BlockingQueue&)            = delete;
    BlockingQueue& operator=(const BlockingQueue&) = delete;

    // Push one item. Returns false if the queue is already closed (the item is
    // NOT enqueued) — a bool beats throwing here: "closed" is an expected outcome
    // in shutdown, not an exceptional one.
    bool push(T value) {
        {
            std::lock_guard lk(m_);          // state change UNDER the lock (Q3)
            if (closed_) {
                return false;
            }
            q_.push(std::move(value));
        }                                     // <-- unlock BEFORE notify:
        cv_.notify_one();                     //     the woken consumer doesn't
        return true;                          //     immediately re-block on m_.
    }

    // Block until an item is available, or the queue is closed and drained.
    // Returns nullopt only when closed AND empty = "nothing more will ever come".
    std::optional<T> pop() {
        std::unique_lock lk(m_);              // unique_lock: wait() must unlock/relock (Q1)
        cv_.wait(lk, [this] {                 // predicate overload = the while-loop (Q2)
            return !q_.empty() || closed_;
        });
        if (q_.empty()) {                     // woken + empty ⇒ closed_ is true ⇒ drained
            return std::nullopt;
        }
        T value = std::move(q_.front());
        q_.pop();
        return value;
    }

    // Non-blocking drain: pop if something is there, else nullopt right away.
    // Does not care about closed_ — an open-but-empty queue also yields nullopt.
    std::optional<T> try_pop() {
        std::lock_guard lk(m_);
        if (q_.empty()) {
            return std::nullopt;
        }
        T value = std::move(q_.front());
        q_.pop();
        return value;
    }

    // Wake every blocked consumer and refuse further pushes. notify_all (not _one)
    // because ONE state change (closing) must serve ALL waiters at once (Q4/Q5).
    void close() {
        {
            std::lock_guard lk(m_);
            closed_ = true;
        }
        cv_.notify_all();
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
    std::queue<T>                   q_;
    mutable std::mutex              m_;        // mutable: closed()/size() are const but must lock
    std::condition_variable         cv_;
    bool                            closed_ = false;
};

}  // namespace cr
