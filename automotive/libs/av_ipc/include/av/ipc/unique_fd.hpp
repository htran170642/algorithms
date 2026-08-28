#pragma once

// RAII for a file descriptor.
//
// Every Linux IPC mechanism in this library -- sockets, epoll, shared memory --
// is a file descriptor, and every one of them leaks if an early return skips
// the close(). This is the Rule of 5 written out once so nothing below has to
// write it again.

#include <unistd.h>

#include <utility>

namespace av::ipc {

class UniqueFd {
public:
    UniqueFd() noexcept = default;
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}

    // Copying would close the same descriptor twice. There is no sane copy of
    // an owned fd, so there is no copy -- callers that need one say dup().
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    ~UniqueFd() { reset(); }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return valid(); }

    /// Hands ownership to the caller. Used when a syscall takes over the fd.
    int release() noexcept { return std::exchange(fd_, -1); }

    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) {
            // close() can return EINTR, but on Linux the descriptor is released
            // regardless -- retrying would close whatever fd was handed out in
            // the meantime, which is far worse than ignoring the error.
            static_cast<void>(::close(fd_));
        }
        fd_ = fd;
    }

private:
    int fd_{-1};
};

}  // namespace av::ipc
