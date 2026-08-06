#pragma once

// W36 — a ThreadPool. The concurrency capstone: it composes three earlier bricks.
//   * jthread (W32)         — N worker threads that auto-join on destruction.
//   * a task queue (W33/34) — mutex + condition_variable, the blocking-queue
//                             pattern, holding units of work.
//   * packaged_task (W35)   — each submitted callable is wired to a future so the
//                             caller gets the result (or the exception) back.
//
// LỐI A — "shared_ptr wrapper" variant.
//   The queue holds one uniform type: std::function<void()>. But packaged_task is
//   MOVE-ONLY, and std::function needs a COPYABLE target. The trick: put the
//   packaged_task<R()> behind a std::shared_ptr (copyable), and enqueue a small
//   std::function<void()> that copies the shared_ptr and invokes it. Sidesteps the
//   move-only problem entirely; compiles under BOTH C++20 and C++23 — no
//   std::move_only_function (C++23-only) needed. Cost: one extra control-block
//   heap alloc per task. Lối B removes it — that's the next exercise.

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace cr {

class ThreadPool {
public:
    // One worker per hardware thread. hardware_concurrency() may report 0 when it
    // can't tell — never spin up a pool with zero workers.
    explicit ThreadPool(unsigned n = std::max(1u, std::thread::hardware_concurrency())) {
        workers_.reserve(n);
        for (unsigned i = 0; i < n; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    // Graceful shutdown: flip stop_, wake everyone, let each jthread's dtor join.
    // Workers exit only once the queue is drained, so queued work still runs.
    ~ThreadPool() {
        {
            std::lock_guard lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
        // workers_ destroyed here: each jthread joins. Queued tasks finish first.
    }

    // Owns unique OS threads and a mutex — neither copyable nor movable.
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&)                 = delete;
    ThreadPool& operator=(ThreadPool&&)      = delete;

    // Enqueue f(args...) to run on some worker; hand back a future for its result.
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using R = std::invoke_result_t<F, Args...>;

        // Bind callable + args into a nullary packaged_task<R()>. shared_ptr makes
        // the move-only packaged_task copyable so a std::function can hold it.
        auto task = std::make_shared<std::packaged_task<R()>>(
            [f = std::forward<F>(f), ... args = std::forward<Args>(args)]() mutable -> R {
                return std::invoke(std::move(f), std::move(args)...);
            });

        std::future<R> result = task->get_future();
        {
            std::lock_guard lk(m_);
            if (stop_) {
                // Enqueuing now would leave work no worker will ever run -> the
                // future would hang forever. Fail loudly instead.
                throw std::runtime_error("cr::ThreadPool::submit on a stopped pool");
            }
            tasks_.emplace([task] { (*task)(); });  // copy the shared_ptr into the queue
        }
        cv_.notify_one();  // one task appeared -> wake one worker
        return result;
    }

    [[nodiscard]] std::size_t worker_count() const noexcept { return workers_.size(); }

private:
    void worker_loop() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock lk(m_);
                cv_.wait(lk, [this] { return stop_ || !tasks_.empty(); });
                // Graceful exit: leave only after stop_ AND queue empty, so
                // everything already submitted still gets run.
                if (stop_ && tasks_.empty()) {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }                // release the lock BEFORE running the task, otherwise
            task();          // the pool would serialize on the mutex.
        }
    }

    std::queue<std::function<void()>> tasks_;
    std::mutex                        m_;
    std::condition_variable           cv_;
    bool                              stop_ = false;
    std::vector<std::jthread>         workers_;
};

}  // namespace cr
