#pragma once

// A thin epoll wrapper.
//
// The problem epoll solves: one thread has to watch several file descriptors
// at once -- a CAN socket, a control socket, a timer -- and blocking on any
// single read() means the others are ignored until it returns. The naive
// alternatives are a thread per fd (expensive, and now everything needs
// locking) or polling in a loop (burns a core to notice nothing happened).
//
// epoll registers the interest once and blocks until *something* is ready,
// which is why it scales where select() and poll() do not: they re-scan the
// whole fd set on every call, epoll keeps it in the kernel.

#include <chrono>
#include <optional>
#include <utility>
#include <vector>

#include "av/ipc/unique_fd.hpp"

namespace av::ipc {

struct PollEvent {
    int fd{-1};
    bool readable{false};
    bool writable{false};
    /// The peer went away, or the fd errored. Always handle this: a hung-up fd
    /// stays "ready" forever, so ignoring it turns epoll_wait into a spin loop.
    bool hangup{false};
};

class Poller {
public:
    static std::optional<Poller> create();

    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;
    Poller(Poller&&) noexcept = default;
    Poller& operator=(Poller&&) noexcept = default;
    ~Poller() = default;

    /// Watches `fd` for readability. EPOLLRDHUP is always included so a peer
    /// disconnect arrives as an event rather than as a read of zero bytes that
    /// only shows up once somebody happens to try.
    bool watch_readable(int fd);
    bool unwatch(int fd);

    /// Blocks up to `timeout` (negative means forever) and returns the events
    /// that fired.
    ///
    /// The returned reference points at a buffer owned by this Poller and is
    /// valid until the next call to wait(). Reusing it is deliberate: a fresh
    /// vector per call would allocate on the hottest path in the process.
    const std::vector<PollEvent>& wait(std::chrono::milliseconds timeout);

    [[nodiscard]] int fd() const noexcept { return epoll_fd_.get(); }

private:
    explicit Poller(UniqueFd fd) : epoll_fd_(std::move(fd)) {}

    UniqueFd epoll_fd_;
    std::vector<PollEvent> ready_;
};

}  // namespace av::ipc
