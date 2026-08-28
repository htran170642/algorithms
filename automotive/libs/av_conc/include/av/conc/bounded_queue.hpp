#pragma once

// A fixed-capacity, thread-safe queue with an explicit close.
//
// This is the seam between the cockpit's threads: the CAN receiver hands
// frames to the decoder here, the decoder hands vehicle state to the UI here.
// Two properties matter more than throughput:
//
//   Bounded  - a slow consumer must not let memory grow without limit. When
//              the queue is full the producer learns about it and decides what
//              to do; the queue never decides for it.
//   Closable - shutdown must not deadlock. close() wakes every blocked thread,
//              and a closed queue still hands out what is already in it.

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace av::conc {

/// What happened to a push. `Full` and `Closed` are different situations: one
/// is backpressure and the caller may retry, the other is shutdown and it
/// must not.
enum class PushStatus : std::uint8_t { Ok, Full, Closed };

template <typename T>
class BoundedQueue {
    // The ring buffer is allocated once, so every slot must already hold a
    // valid object. Trading this constraint for zero allocation on the data
    // path is the same decision CanFrame made with std::array over vector.
    static_assert(std::is_default_constructible_v<T>,
                  "BoundedQueue pre-allocates its slots, so T must be default-constructible");

public:
    /// Throws std::invalid_argument on a capacity of 0. Construction happens
    /// once at start-up, far from the data path, so failing loudly there is
    /// cheaper than carrying a "maybe unusable" queue around.
    explicit BoundedQueue(std::size_t capacity)
        : slots_(capacity), capacity_(capacity) {
        if (capacity == 0U) {
            throw std::invalid_argument("BoundedQueue capacity must be at least 1");
        }
    }

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;
    BoundedQueue(BoundedQueue&&) = delete;
    BoundedQueue& operator=(BoundedQueue&&) = delete;
    ~BoundedQueue() = default;

    /// Blocks until there is room, or the queue is closed.
    ///
    /// Use this where blocking is the correct backpressure: the producer can
    /// afford to wait and you would rather slow it down than lose data.
    PushStatus push(T value) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            not_full_.wait(lock, [this] { return count_ < capacity_ || closed_; });
            if (closed_) {
                return PushStatus::Closed;
            }
            emplace(std::move(value));
        }
        // Notify outside the lock: waking a thread that then immediately
        // blocks on the mutex we still hold is wasted work.
        not_empty_.notify_one();
        return PushStatus::Ok;
    }

    /// Never blocks. Returns `Full` so the caller can choose its own policy --
    /// drop this item, drop the oldest, or count it and move on.
    ///
    /// This is what a CAN receive thread uses: it cannot stall, because the
    /// kernel socket buffer behind it would overflow and lose frames anyway.
    PushStatus try_push(T value) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return PushStatus::Closed;
            }
            if (count_ == capacity_) {
                return PushStatus::Full;
            }
            emplace(std::move(value));
        }
        not_empty_.notify_one();
        return PushStatus::Ok;
    }

    /// Blocks until an item is available, or the queue is closed *and empty*.
    ///
    /// The "and empty" is what makes shutdown graceful: closing stops new work
    /// from arriving but never discards work already queued. A consumer loop
    /// `while (auto item = queue.pop())` therefore drains and then exits on
    /// its own, with no separate stop flag.
    std::optional<T> pop() {
        std::optional<T> value;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            not_empty_.wait(lock, [this] { return count_ > 0U || closed_; });
            if (count_ == 0U) {
                return std::nullopt;  // closed and drained -- the only way out
            }
            value = take();
        }
        not_full_.notify_one();
        return value;
    }

    /// Never blocks. Empty and closed-and-empty are both nullopt here; use
    /// closed() if the difference matters.
    std::optional<T> try_pop() {
        std::optional<T> value;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (count_ == 0U) {
                return std::nullopt;
            }
            value = take();
        }
        not_full_.notify_one();
        return value;
    }

    /// Refuses further pushes and wakes everyone currently blocked.
    ///
    /// notify_all, not notify_one: every waiter has to observe the new state,
    /// and a single notification would leave the rest asleep forever.
    /// Idempotent, so several shutdown paths may call it.
    void close() {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_full_.notify_all();
        not_empty_.notify_all();
    }

    [[nodiscard]] bool closed() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    /// A snapshot that is already stale when it is returned. Fine for logging
    /// and for tests that have quiesced the queue; never branch on it in
    /// production code -- that is a race by construction.
    [[nodiscard]] std::size_t size() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    // Both helpers require the caller to hold mutex_.
    void emplace(T&& value) {
        slots_[tail_] = std::move(value);
        tail_ = (tail_ + 1U) % capacity_;
        ++count_;
    }

    T take() {
        T value = std::move(slots_[head_]);
        head_ = (head_ + 1U) % capacity_;
        --count_;
        return value;
    }

    mutable std::mutex mutex_;
    // Two condition variables, not one: a consumer finishing a pop should wake
    // a waiting *producer*, not another consumer. Sharing one variable would
    // wake everybody and let most of them go straight back to sleep.
    std::condition_variable not_full_;
    std::condition_variable not_empty_;

    std::vector<T> slots_;
    std::size_t capacity_;
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t count_{0};
    bool closed_{false};
};

}  // namespace av::conc
