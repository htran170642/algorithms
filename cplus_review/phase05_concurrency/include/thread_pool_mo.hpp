#pragma once

// W36 — LỐI B, "move-only queue element" variant of the ThreadPool.
//
// Lối A wrapped the packaged_task in a shared_ptr purely to make it COPYABLE,
// because std::function<void()> demands a copyable target. Drop that crutch: if
// the queue element is itself MOVE-ONLY, the shared_ptr disappears.
//
//   * C++23 -> std::move_only_function<void()>  — a move-only, type-erased
//              callable with NO shared state. This is exactly the hole C++23
//              added move_only_function to fill.
//   * C++20 -> std::packaged_task<void()> as the fallback. It is move-only, so
//              it works — but it carries its OWN shared state that we never use,
//              one wasted heap alloc per task. Honest about the cost.
//
// Everything else (worker loop, graceful shutdown, member order) is identical to
// Lối A — the ONLY thing that changed is the queue element type.

#include <condition_variable>
#include <cstddef>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <version>   // feature-test macros: __cpp_lib_move_only_function

#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
#  include <functional>          // std::move_only_function  (C++23)
#  define CR_HAVE_MOVE_ONLY_FUNCTION 1
#else
#  define CR_HAVE_MOVE_ONLY_FUNCTION 0
#endif

namespace cr {

class ThreadPoolMO {
    // The move-only callable the queue holds. No shared_ptr, no copies.
#if CR_HAVE_MOVE_ONLY_FUNCTION
    using Task = std::move_only_function<void()>;
#else
    using Task = std::packaged_task<void()>;   // move-only fallback for C++20
#endif

public:
    explicit ThreadPoolMO(unsigned n = std::max(1u, std::thread::hardware_concurrency())) {
        workers_.reserve(n);
        for (unsigned i = 0; i < n; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPoolMO() {
        {
            std::lock_guard lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
        // workers_ (declared LAST) destroyed first: each jthread joins while
        // m_/cv_/tasks_ are still alive. Get this order wrong -> SIGSEGV.
    }

    ThreadPoolMO(const ThreadPoolMO&)            = delete;
    ThreadPoolMO& operator=(const ThreadPoolMO&) = delete;
    ThreadPoolMO(ThreadPoolMO&&)                 = delete;
    ThreadPoolMO& operator=(ThreadPoolMO&&)      = delete;

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using R = std::invoke_result_t<F, Args...>;

        // The inner packaged_task<R()> carries the caller's future.
        std::packaged_task<R()> pt(
            [f = std::forward<F>(f), ... args = std::forward<Args>(args)]() mutable -> R {
                return std::invoke(std::move(f), std::move(args)...);
            });

        std::future<R> result = pt.get_future();
        {
            std::lock_guard lk(m_);
            if (stop_) {
                throw std::runtime_error("cr::ThreadPoolMO::submit on a stopped pool");
            }
            // Capture the move-only packaged_task BY MOVE into a mutable lambda,
            // and store the lambda directly. No copyable requirement -> no
            // shared_ptr. `mutable` because packaged_task::operator() is non-const.
            tasks_.emplace([pt = std::move(pt)]() mutable { pt(); });
        }
        cv_.notify_one();
        return result;
    }

    [[nodiscard]] std::size_t worker_count() const noexcept { return workers_.size(); }

private:
    void worker_loop() {
        for (;;) {
            Task task;
            {
                std::unique_lock lk(m_);
                cv_.wait(lk, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) {
                    return;
                }
                task = std::move(tasks_.front());   // move-only: move, never copy
                tasks_.pop();
            }
            task();   // run outside the lock so the pool doesn't serialize
        }
    }

    std::queue<Task>          tasks_;
    std::mutex                m_;
    std::condition_variable   cv_;
    bool                      stop_ = false;
    std::vector<std::jthread> workers_;   // LAST -> destroyed first -> joins
};

}  // namespace cr
