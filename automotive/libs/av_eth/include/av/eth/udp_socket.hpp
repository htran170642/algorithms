#pragma once

// UDP over IPv4, with the multicast controls automotive Ethernet actually uses.
//
// Week 3 crossed an address space with AF_UNIX. Week 5 crossed a vehicle bus
// with PF_CAN. This crosses a *network*, and the difference is not the API --
// it is socket(), bind(), sendto(), recvfrom() again -- but the guarantees that
// disappear on the way:
//
//     CAN                          UDP / Ethernet
//     ----------------------       ---------------------------
//     frames arrive in order       may arrive out of order
//     a frame is retried until     a datagram is dropped in
//       somebody ACKs it             silence, by any switch
//     priority is decided by        priority is a hint (DSCP /
//       arbitration, in hardware      VLAN PCP) the switch may ignore
//     every node hears every        a switch forwards only where it
//       frame, always                 believes a listener exists
//
// None of that is a defect. It is the trade for 100 Mbit/s instead of 1, and
// for 1500-byte payloads instead of 8. But it means an application that used
// to get ordering and delivery for free now has to notice their absence by
// itself -- which is what apps/eth_sub does with a sequence number.
//
// Why multicast rather than a TCP connection per consumer:
//
//     Cluster ──┐
//     IVI ──────┼── all want the same vehicle signals
//     Logger ───┘
//
// With unicast the gateway sends the same bytes N times and must know who is
// listening. With multicast it sends once, to a group address, and the switch
// replicates only towards ports that asked (IGMP). The publisher does not
// learn who the subscribers are -- the same decoupling SOME/IP's event model
// gives at a higher layer in week 8.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "av/ipc/unique_fd.hpp"

namespace av::eth {

/// The largest UDP payload that still fits one standard Ethernet frame:
/// 1500 MTU - 20 IPv4 header - 8 UDP header.
///
/// Exceeding it is legal and works: IP fragments the datagram. It is also a
/// bad idea on a vehicle network, because losing *any* fragment discards the
/// whole datagram, so a 2-fragment message is roughly twice as likely to
/// vanish as a 1-fragment one.
inline constexpr std::size_t kMaxDatagram = 1472;

/// An address and a port. Kept as text because that is what a config file, a
/// log line and a human all use; the conversion happens at the syscall.
struct Endpoint {
    std::string address;
    std::uint16_t port{0};
};

/// True for 224.0.0.0/4. Worth checking explicitly: sending to a multicast
/// group works whether or not the socket was configured for multicast, and the
/// mistake shows up as "the packets exist but nobody outside this host sees
/// them" -- a TTL problem, not an address problem.
[[nodiscard]] bool is_multicast(std::string_view address) noexcept;

/// Same contract as av::can::SocketStatus: WouldBlock is a normal state on a
/// non-blocking socket, not a failure.
enum class SocketStatus : std::uint8_t {
    Ok,
    WouldBlock,
    Error,
};

struct ReceiveResult {
    SocketStatus status{SocketStatus::Error};
    std::size_t size{0};  ///< bytes written into the caller's buffer
    Endpoint from{};      ///< who sent it -- UDP has no connection to ask
};

class UdpSocket {
public:
    static std::optional<UdpSocket> open();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&&) noexcept = default;
    UdpSocket& operator=(UdpSocket&&) noexcept = default;
    ~UdpSocket() = default;

    /// Binds to `port` on every interface.
    ///
    /// SO_REUSEADDR is set first, and for multicast that is not optional: two
    /// subscribers on one host must both bind the same port, and without it
    /// the second one fails with EADDRINUSE.
    bool bind_any(std::uint16_t port);

    /// Joins a multicast group, i.e. sends an IGMP membership report so the
    /// switch starts forwarding that group to this port.
    ///
    /// `interface_address` picks *which* NIC joins. "0.0.0.0" lets the kernel
    /// choose by routing table, which is fine on a laptop and wrong on an ECU
    /// with a vehicle NIC and a diagnostics NIC: the join lands on one of them
    /// and the traffic arrives on the other. Nothing reports an error; the
    /// subscriber is simply silent.
    bool join_multicast(std::string_view group, std::string_view interface_address = "0.0.0.0");

    /// Chooses the NIC that *outgoing* multicast leaves by. Separate from
    /// join_multicast() because sending and receiving are separate decisions.
    bool set_multicast_interface(std::string_view interface_address);

    /// Hop limit for outgoing multicast. The default is 1, which is what a
    /// vehicle network wants: the datagram reaches every node on the local
    /// link and no router will ever forward it off the vehicle.
    bool set_multicast_ttl(unsigned ttl);

    /// Whether this host's own sockets receive what this socket sends.
    ///
    /// On by default. That default is why a gateway and a subscriber can be
    /// tested on one machine at all -- and also why a program that both
    /// publishes and subscribes sees its own traffic and mistakes it for the
    /// peer's.
    bool set_multicast_loopback(bool enabled);

    /// Sets the DSCP code point in the IP header (the top 6 bits of the TOS
    /// byte). This is the Ethernet answer to CAN's arbitration -- with one
    /// enormous difference: CAN priority is enforced by physics on every bit,
    /// while DSCP is a *request* that each switch is free to honour, remap, or
    /// erase. It does nothing at all unless the network was configured for it.
    bool set_dscp(unsigned code_point);

    bool set_non_blocking();

    SocketStatus send_to(const Endpoint& destination, const void* data, std::size_t size);

    /// Reads one datagram into `buffer`. A datagram is atomic: one recvfrom()
    /// returns exactly one send_to(), never half of one and never two joined.
    /// That is the property TCP does not have, and the reason UDP needs no
    /// framing layer while a TCP stream does.
    ReceiveResult receive(std::vector<std::uint8_t>& buffer);

    /// For av::ipc::Poller. Week 3's epoll loop works unchanged, again.
    [[nodiscard]] int fd() const noexcept { return fd_.get(); }

private:
    explicit UdpSocket(ipc::UniqueFd fd) : fd_(std::move(fd)) {}

    ipc::UniqueFd fd_;
};

}  // namespace av::eth
