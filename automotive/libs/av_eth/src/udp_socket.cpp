#include "av/eth/udp_socket.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "av/ipc/unique_fd.hpp"
#include "av/log.hpp"

namespace av::eth {
namespace {

namespace log = av::log;

/// inet_pton needs a NUL-terminated string and string_view is not one.
///
/// The copy does not allocate: the longest dotted-quad is 15 characters, well
/// inside every mainstream std::string's small-buffer optimisation. That is
/// what lets is_multicast() stay noexcept.
bool parse_address(std::string_view text, in_addr& out) {
    const std::string copy{text};
    return ::inet_pton(AF_INET, copy.c_str(), &out) == 1;
}

SocketStatus classify_errno(int error) noexcept {
    // Same value on Linux, not guaranteed to be by POSIX.
    if (error == EAGAIN || error == EWOULDBLOCK) {
        return SocketStatus::WouldBlock;
    }
    return SocketStatus::Error;
}

/// Every socket option here is an int-valued one, so the boilerplate is
/// written once.
bool set_int_option(int fd, int level, int option, int value, const char* what) {
    if (::setsockopt(fd, level, option, &value, static_cast<socklen_t>(sizeof(value))) < 0) {
        log::error("eth.udp", "setsockopt failed", "option", what, "errno", errno);
        return false;
    }
    return true;
}

}  // namespace

bool is_multicast(std::string_view address) noexcept {
    in_addr parsed{};
    if (!parse_address(address, parsed)) {
        return false;
    }
    // 224.0.0.0/4 -- the top four bits are 1110.
    return (::ntohl(parsed.s_addr) & 0xF000'0000U) == 0xE000'0000U;
}

std::optional<UdpSocket> UdpSocket::open() {
    ipc::UniqueFd fd{::socket(AF_INET, SOCK_DGRAM, 0)};
    if (!fd) {
        log::error("eth.udp", "socket() failed", "errno", errno);
        return std::nullopt;
    }
    return UdpSocket{std::move(fd)};
}

bool UdpSocket::bind_any(std::uint16_t port) {
    // Before bind(), never after: the kernel checks the flag while binding.
    if (!set_int_option(fd_.get(), SOL_SOCKET, SO_REUSEADDR, 1, "SO_REUSEADDR")) {
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = ::htons(port);
    address.sin_addr.s_addr = ::htonl(INADDR_ANY);

    if (::bind(fd_.get(), reinterpret_cast<const sockaddr*>(&address),
               static_cast<socklen_t>(sizeof(address))) < 0) {
        log::error("eth.udp", "bind failed", "port", port, "errno", errno);
        return false;
    }
    return true;
}

bool UdpSocket::join_multicast(std::string_view group, std::string_view interface_address) {
    if (!is_multicast(group)) {
        log::error("eth.udp", "not a multicast group", "address", group, "expected", "224.0.0.0/4");
        return false;
    }

    ip_mreq request{};
    if (!parse_address(group, request.imr_multiaddr) ||
        !parse_address(interface_address, request.imr_interface)) {
        log::error("eth.udp", "bad address", "group", group, "interface", interface_address);
        return false;
    }

    if (::setsockopt(fd_.get(), IPPROTO_IP, IP_ADD_MEMBERSHIP, &request,
                     static_cast<socklen_t>(sizeof(request))) < 0) {
        log::error("eth.udp", "IP_ADD_MEMBERSHIP failed", "group", group, "interface",
                   interface_address, "errno", errno);
        return false;
    }
    return true;
}

bool UdpSocket::set_multicast_interface(std::string_view interface_address) {
    in_addr address{};
    if (!parse_address(interface_address, address)) {
        log::error("eth.udp", "bad interface address", "address", interface_address);
        return false;
    }
    if (::setsockopt(fd_.get(), IPPROTO_IP, IP_MULTICAST_IF, &address,
                     static_cast<socklen_t>(sizeof(address))) < 0) {
        log::error("eth.udp", "IP_MULTICAST_IF failed", "address", interface_address, "errno",
                   errno);
        return false;
    }
    return true;
}

bool UdpSocket::set_multicast_ttl(unsigned ttl) {
    if (ttl > 255U) {
        log::error("eth.udp", "ttl out of range", "ttl", ttl);
        return false;
    }
    return set_int_option(fd_.get(), IPPROTO_IP, IP_MULTICAST_TTL, static_cast<int>(ttl),
                          "IP_MULTICAST_TTL");
}

bool UdpSocket::set_multicast_loopback(bool enabled) {
    return set_int_option(fd_.get(), IPPROTO_IP, IP_MULTICAST_LOOP, enabled ? 1 : 0,
                          "IP_MULTICAST_LOOP");
}

bool UdpSocket::set_dscp(unsigned code_point) {
    if (code_point > 63U) {
        log::error("eth.udp", "dscp out of range", "code_point", code_point, "max", 63);
        return false;
    }
    // DSCP is the top 6 bits of the TOS byte; the low 2 are ECN and belong to
    // the stack, not to us.
    return set_int_option(fd_.get(), IPPROTO_IP, IP_TOS, static_cast<int>(code_point << 2U),
                          "IP_TOS");
}

bool UdpSocket::set_non_blocking() {
    const int flags = ::fcntl(fd_.get(), F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    // Read-modify-write: clobbering the flag word would clear anything else
    // the descriptor carries.
    return ::fcntl(fd_.get(), F_SETFL, flags | O_NONBLOCK) == 0;
}

SocketStatus UdpSocket::send_to(const Endpoint& destination, const void* data, std::size_t size) {
    if (size > kMaxDatagram) {
        // Refused rather than fragmented. See the note on kMaxDatagram: a
        // fragmented datagram is lost whenever any one of its fragments is.
        log::error("eth.udp", "datagram would fragment", "size", size, "limit", kMaxDatagram);
        return SocketStatus::Error;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = ::htons(destination.port);
    if (!parse_address(destination.address, address.sin_addr)) {
        log::error("eth.udp", "bad destination address", "address", destination.address);
        return SocketStatus::Error;
    }

    const auto sent =
        ::sendto(fd_.get(), data, size, 0, reinterpret_cast<const sockaddr*>(&address),
                 static_cast<socklen_t>(sizeof(address)));
    if (sent < 0) {
        const SocketStatus status = classify_errno(errno);
        if (status == SocketStatus::Error) {
            log::error("eth.udp", "sendto failed", "address", destination.address, "port",
                       destination.port, "errno", errno);
        }
        return status;
    }
    // A datagram is all-or-nothing, so a short write is not a partial send to
    // resume -- it is a bug worth reporting rather than papering over.
    if (static_cast<std::size_t>(sent) != size) {
        log::error("eth.udp", "short datagram write", "sent", sent, "expected", size);
        return SocketStatus::Error;
    }
    return SocketStatus::Ok;
}

ReceiveResult UdpSocket::receive(std::vector<std::uint8_t>& buffer) {
    ReceiveResult result;

    // Sized once, then reused. A datagram longer than the buffer is silently
    // truncated by recvfrom(), so the buffer is never smaller than the largest
    // unfragmented datagram this socket can legally be sent.
    if (buffer.size() < kMaxDatagram) {
        buffer.resize(kMaxDatagram);
    }

    sockaddr_in from{};
    auto from_size = static_cast<socklen_t>(sizeof(from));
    const auto got = ::recvfrom(fd_.get(), buffer.data(), buffer.size(), 0,
                                reinterpret_cast<sockaddr*>(&from), &from_size);
    if (got < 0) {
        result.status = classify_errno(errno);
        if (result.status == SocketStatus::Error) {
            log::error("eth.udp", "recvfrom failed", "errno", errno);
        }
        return result;
    }

    result.status = SocketStatus::Ok;
    result.size = static_cast<std::size_t>(got);

    std::array<char, INET_ADDRSTRLEN> text{};
    if (::inet_ntop(AF_INET, &from.sin_addr, text.data(),
                    static_cast<socklen_t>(text.size())) != nullptr) {
        result.from.address = text.data();
    }
    result.from.port = ::ntohs(from.sin_port);
    return result;
}

}  // namespace av::eth
