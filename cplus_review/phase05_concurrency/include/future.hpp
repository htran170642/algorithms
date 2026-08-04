#pragma once

// W35 — a mini Future<T> / Promise<T>, to see what std::future is INSIDE.
//
// A future/promise is a ONE-SHOT, single-value channel between two threads:
//   * Promise<T> is the WRITE end  — set_value() / set_exception(), called once.
//   * Future<T>  is the READ  end  — get() blocks until the value (or exception)
//                                    arrives, then consumes it.
//
// The thing they share is a heap-allocated "shared state": the value slot, an
// error slot (std::exception_ptr — this is how an exception travels across the
// thread boundary), a ready flag, and a mutex+cv to block the reader until ready.
// We refcount it with shared_ptr so either end may outlive the other.
//
// Deliberately faithful to std::: get() is one-shot (consumes the state),
// get_future() is one-shot, set_* twice throws promise_already_satisfied, and an
// exception set on the write end is RETHROWN out of get() on the read end.
//
// What we do NOT reproduce: std::async's blocking-destructor quirk (that lives in
// async(), not in future) and launch policies. Those are demonstrated against the
// real std:: facilities in the test — no point cloning what the standard ships.

#include <condition_variable>
#include <exception>
#include <future>   // std::future_error / std::future_errc — reuse the real error type
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace cr {

// The channel both ends share. One allocation, refcounted; guarded by m.
template <typename T>
struct SharedState {
    std::mutex              m;
    std::condition_variable cv;
    std::optional<T>        value;
    std::exception_ptr      error;
    bool                    ready = false;
};

template <typename T>
class Future {
public:
    Future() = default;
    explicit Future(std::shared_ptr<SharedState<T>> state) : state_(std::move(state)) {}

    // Has this future not yet been consumed by get()?  (mirrors std::future::valid)
    [[nodiscard]] bool valid() const noexcept { return state_ != nullptr; }

    // Block until ready, then hand back the value — or RETHROW the exception the
    // producer set. One-shot: we move the shared_ptr out, so a second get() on the
    // same future is a use of a moved-from (invalid) future, exactly like std::.
    T get() {
        auto state = std::move(state_);                 // future is now invalid
        std::unique_lock lk(state->m);
        state->cv.wait(lk, [&] { return state->ready; });
        if (state->error) {
            std::rethrow_exception(state->error);       // exception crosses the thread here
        }
        return std::move(*state->value);
    }

    // Block until ready without consuming — you can wait() then get().
    void wait() const {
        std::unique_lock lk(state_->m);
        state_->cv.wait(lk, [&] { return state_->ready; });
    }

private:
    std::shared_ptr<SharedState<T>> state_;
};

template <typename T>
class Promise {
public:
    Promise() : state_(std::make_shared<SharedState<T>>()) {}

    // A promise is move-only (it OWNS the write end of a unique channel), just
    // like std::promise. The compiler-generated move is correct: shared_ptr moves,
    // the bool copies. Copy is meaningless (two writers to one slot) -> deleted.
    Promise(const Promise&)                = delete;
    Promise& operator=(const Promise&)     = delete;
    Promise(Promise&&) noexcept            = default;
    Promise& operator=(Promise&&) noexcept = default;

    // The read end. One-shot: a second call throws, as std::promise does.
    Future<T> get_future() {
        if (future_retrieved_) {
            throw std::future_error(std::future_errc::future_already_retrieved);
        }
        future_retrieved_ = true;
        return Future<T>(state_);
    }

    void set_value(T value) {
        {
            std::lock_guard lk(state_->m);
            throw_if_satisfied();
            state_->value = std::move(value);
            state_->ready = true;
        }
        state_->cv.notify_all();                         // wake every waiter
    }

    // Publish an exception instead of a value. get() on the reader rethrows it.
    void set_exception(std::exception_ptr e) {
        {
            std::lock_guard lk(state_->m);
            throw_if_satisfied();
            state_->error = std::move(e);
            state_->ready = true;
        }
        state_->cv.notify_all();
    }

private:
    void throw_if_satisfied() const {                    // called under m
        if (state_->ready) {
            throw std::future_error(std::future_errc::promise_already_satisfied);
        }
    }

    std::shared_ptr<SharedState<T>> state_;
    bool                            future_retrieved_ = false;
};

}  // namespace cr
