#pragma once

// A Unix domain socket, SOCK_SEQPACKET.
//
// SEQPACKET rather than STREAM or DGRAM, because it is the only one of the
// three that gives all of: message boundaries preserved, delivery reliable and
// ordered, and peer disconnect detectable. A CanFrame sent as one message
// arrives as one message -- no framing header, no partial reads to stitch back
// together. That framing problem is the single largest source of bugs when
// people reach for SOCK_STREAM out of habit.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "av/ipc/unique_fd.hpp"

namespace av::ipc {

/// Why an I/O call did not simply succeed.
///
/// `WouldBlock` is not an error -- on a non-blocking fd it is the normal
/// answer meaning "come back when epoll says so". Collapsing it into `Error`
/// is how a poll loop turns into a busy-wait or a spurious shutdown.
enum class IoStatus : std::uint8_t {
    Ok,
    WouldBlock,
    PeerClosed,
    Error,
};

struct ReceiveResult {
    IoStatus status{IoStatus::Error};
    std::size_t bytes{0};
};

class UnixSocket {
public:
    /// Creates the listening endpoint and the socket file at `path`.
    ///
    /// Unlinks a stale file left by a previous crashed run first: bind() fails
    /// with EADDRINUSE against a socket file whose owner is long gone, and a
    /// cockpit that will not restart after a crash is worse than one that
    /// replaces a dead endpoint.
    static std::optional<UnixSocket> listen(std::string_view path, int backlog = 4);

    /// Connects to an existing endpoint.
    static std::optional<UnixSocket> connect(std::string_view path);

    /// Blocks (or returns nullopt on a non-blocking socket with no pending
    /// connection) until a peer connects.
    std::optional<UnixSocket> accept();

    UnixSocket(const UnixSocket&) = delete;
    UnixSocket& operator=(const UnixSocket&) = delete;
    UnixSocket(UnixSocket&&) noexcept = default;
    UnixSocket& operator=(UnixSocket&&) noexcept = default;
    ~UnixSocket();

    /// Sends one whole message. SEQPACKET either delivers all of it or fails;
    /// there is no partial send to loop over.
    IoStatus send(const void* data, std::size_t size);

    /// Reads one whole message. A message larger than `capacity` is truncated
    /// and the remainder discarded -- that is SEQPACKET semantics, and it is
    /// why the receive buffer must be sized from the protocol, not guessed.
    ReceiveResult receive(void* buffer, std::size_t capacity);

    /// Required before handing the fd to a Poller: a blocking fd defeats the
    /// entire point of epoll, because one slow peer stalls every other one.
    bool set_non_blocking();

    [[nodiscard]] int fd() const noexcept { return fd_.get(); }
    [[nodiscard]] bool valid() const noexcept { return fd_.valid(); }

private:
    explicit UnixSocket(UniqueFd fd, std::string owned_path = {})
        : fd_(std::move(fd)), owned_path_(std::move(owned_path)) {}

    UniqueFd fd_;
    /// Non-empty only on the listener that created the file. Closing a socket
    /// does not remove its path from the filesystem; somebody has to unlink it
    /// and it has to be the process that made it.
    std::string owned_path_;
};

}  // namespace av::ipc
