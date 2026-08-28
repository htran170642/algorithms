#include "av/ipc/poller.hpp"

#include <sys/epoll.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

#include "av/ipc/unique_fd.hpp"
#include "av/log.hpp"

namespace av::ipc {
namespace {

namespace log = av::log;

/// One epoll_wait() call can report at most this many events. Anything beyond
/// it is not lost -- it is simply reported by the next call, because epoll is
/// level-triggered here and a still-ready fd fires again.
constexpr int kMaxEventsPerWait = 64;

}  // namespace

std::optional<Poller> Poller::create() {
    // EPOLL_CLOEXEC: without it the epoll fd survives exec() into a child that
    // has no idea it owns it. Every fd this project creates should be
    // close-on-exec unless it is deliberately being handed to a child.
    UniqueFd fd{::epoll_create1(EPOLL_CLOEXEC)};
    if (!fd) {
        log::error("ipc.poller", "epoll_create1 failed", "errno", std::strerror(errno));
        return std::nullopt;
    }
    return Poller{std::move(fd)};
}

bool Poller::watch_readable(int fd) {
    epoll_event event{};
    // Level-triggered on purpose (no EPOLLET). Edge-triggered is faster but
    // requires draining every fd until EAGAIN on each wakeup; forgetting once
    // means the fd goes silent forever. Level-triggered forgives a partial
    // read, which is the right default until a profile says otherwise.
    event.events = EPOLLIN | EPOLLRDHUP;
    event.data.fd = fd;

    if (::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, fd, &event) != 0) {
        log::error("ipc.poller", "epoll_ctl ADD failed", "fd", fd, "errno", std::strerror(errno));
        return false;
    }
    return true;
}

bool Poller::unwatch(int fd) {
    if (::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr) != 0) {
        // ENOENT is benign: the fd was already removed, or closed (closing an
        // fd removes it from every epoll set automatically).
        if (errno != ENOENT) {
            log::error("ipc.poller", "epoll_ctl DEL failed", "fd", fd, "errno",
                       std::strerror(errno));
            return false;
        }
    }
    return true;
}

const std::vector<PollEvent>& Poller::wait(std::chrono::milliseconds timeout) {
    ready_.clear();

    std::array<epoll_event, kMaxEventsPerWait> events{};
    const int count = ::epoll_wait(epoll_fd_.get(), events.data(), kMaxEventsPerWait,
                                   static_cast<int>(timeout.count()));

    if (count < 0) {
        // EINTR is not a failure: a signal arrived while blocked. The caller
        // loops and calls wait() again. Treating it as an error is how a
        // process dies the first time somebody attaches a debugger.
        if (errno != EINTR) {
            log::error("ipc.poller", "epoll_wait failed", "errno", std::strerror(errno));
        }
        return ready_;
    }

    ready_.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const epoll_event& raw = events[static_cast<std::size_t>(i)];
        const std::uint32_t mask = raw.events;
        ready_.push_back(PollEvent{
            raw.data.fd,
            (mask & EPOLLIN) != 0U,
            (mask & EPOLLOUT) != 0U,
            (mask & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) != 0U,
        });
    }
    return ready_;
}

}  // namespace av::ipc
