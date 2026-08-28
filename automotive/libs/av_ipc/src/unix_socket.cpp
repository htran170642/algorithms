#include "av/ipc/unix_socket.hpp"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "av/ipc/unique_fd.hpp"
#include "av/log.hpp"

namespace av::ipc {
namespace {

namespace log = av::log;

/// sun_path is a fixed 108-byte array, and the kernel wants it NUL-terminated.
/// Anything longer is silently truncated by a naive copy, which then binds a
/// different path than the caller asked for -- so refuse instead.
bool fill_address(sockaddr_un& address, std::string_view path) {
    if (path.empty() || path.size() >= sizeof(address.sun_path)) {
        return false;
    }
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.data(), path.size());
    address.sun_path[path.size()] = '\0';
    return true;
}

/// EAGAIN and EWOULDBLOCK are the same value on Linux but not guaranteed to be
/// by POSIX, so both are checked.
IoStatus classify_errno(int error) {
    if (error == EAGAIN || error == EWOULDBLOCK) {
        return IoStatus::WouldBlock;
    }
    if (error == EPIPE || error == ECONNRESET) {
        return IoStatus::PeerClosed;
    }
    return IoStatus::Error;
}

UniqueFd make_socket() { return UniqueFd{::socket(AF_UNIX, SOCK_SEQPACKET, 0)}; }

}  // namespace

std::optional<UnixSocket> UnixSocket::listen(std::string_view path, int backlog) {
    sockaddr_un address{};
    if (!fill_address(address, path)) {
        log::error("ipc.socket", "path rejected", "path", path, "limit",
                   sizeof(address.sun_path) - 1);
        return std::nullopt;
    }

    UniqueFd fd = make_socket();
    if (!fd) {
        log::error("ipc.socket", "socket() failed", "errno", std::strerror(errno));
        return std::nullopt;
    }

    // A socket file outlives the process that made it. Remove a stale one so a
    // restart after a crash works; ENOENT here is the normal case.
    if (::unlink(address.sun_path) != 0 && errno != ENOENT) {
        log::warn("ipc.socket", "could not remove stale endpoint", "path", path, "errno",
                  std::strerror(errno));
    }

    if (::bind(fd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        log::error("ipc.socket", "bind() failed", "path", path, "errno", std::strerror(errno));
        return std::nullopt;
    }
    if (::listen(fd.get(), backlog) != 0) {
        log::error("ipc.socket", "listen() failed", "path", path, "errno", std::strerror(errno));
        static_cast<void>(::unlink(address.sun_path));
        return std::nullopt;
    }

    return UnixSocket{std::move(fd), std::string{path}};
}

std::optional<UnixSocket> UnixSocket::connect(std::string_view path) {
    sockaddr_un address{};
    if (!fill_address(address, path)) {
        return std::nullopt;
    }

    UniqueFd fd = make_socket();
    if (!fd) {
        return std::nullopt;
    }
    if (::connect(fd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        log::error("ipc.socket", "connect() failed", "path", path, "errno", std::strerror(errno));
        return std::nullopt;
    }

    // The client did not create the file, so it must not unlink it.
    return UnixSocket{std::move(fd)};
}

std::optional<UnixSocket> UnixSocket::accept() {
    UniqueFd peer{::accept(fd_.get(), nullptr, nullptr)};
    if (!peer) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log::error("ipc.socket", "accept() failed", "errno", std::strerror(errno));
        }
        return std::nullopt;
    }
    return UnixSocket{std::move(peer)};
}

UnixSocket::~UnixSocket() {
    if (!owned_path_.empty()) {
        static_cast<void>(::unlink(owned_path_.c_str()));
    }
}

IoStatus UnixSocket::send(const void* data, std::size_t size) {
    // MSG_NOSIGNAL: without it, writing to a socket whose peer has gone raises
    // SIGPIPE and kills the process by default. A cockpit must learn about a
    // dead peer as an error code, not as a signal.
    const ssize_t written = ::send(fd_.get(), data, size, MSG_NOSIGNAL);
    if (written < 0) {
        return classify_errno(errno);
    }
    // SEQPACKET is all-or-nothing, so a short write means the message did not
    // fit the socket buffer -- a configuration problem, not something to retry.
    return (static_cast<std::size_t>(written) == size) ? IoStatus::Ok : IoStatus::Error;
}

ReceiveResult UnixSocket::receive(void* buffer, std::size_t capacity) {
    const ssize_t received = ::recv(fd_.get(), buffer, capacity, 0);
    if (received < 0) {
        return {classify_errno(errno), 0};
    }
    if (received == 0) {
        // On SEQPACKET a zero-length read means the peer closed. (A genuine
        // empty message is possible in theory; this library never sends one.)
        return {IoStatus::PeerClosed, 0};
    }
    return {IoStatus::Ok, static_cast<std::size_t>(received)};
}

bool UnixSocket::set_non_blocking() {
    const int flags = ::fcntl(fd_.get(), F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    // Read-modify-write, not a bare O_NONBLOCK: overwriting the flag word
    // would clear O_APPEND and anything else already set on the descriptor.
    return ::fcntl(fd_.get(), F_SETFL, flags | O_NONBLOCK) == 0;
}

}  // namespace av::ipc
