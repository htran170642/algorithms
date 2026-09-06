// UdpSocket, exercised on the loopback interface.
//
// Two things here need explaining, because both are deliberate:
//
//   1. A fixed port rather than an ephemeral one. An ephemeral bind would need
//      a getsockname() accessor whose only caller is this file. A collision is
//      handled by skipping, not failing -- a port already in use says nothing
//      about the code under test.
//   2. Multicast cases skip when the environment cannot carry them. The `lo`
//      interface does not always have the MULTICAST flag set, and a container
//      may have no multicast-capable interface at all. Reporting that as a
//      failure would train the reader to ignore red runs, which is worse than
//      an honest skip -- the same choice week 5 made for vcan0.

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "av/eth/udp_socket.hpp"

namespace {

using av::eth::Endpoint;
using av::eth::is_multicast;
using av::eth::kMaxDatagram;
using av::eth::SocketStatus;
using av::eth::UdpSocket;

constexpr std::uint16_t kPort = 34590;
constexpr const char* kGroup = "239.10.0.1";
constexpr const char* kLoopback = "127.0.0.1";

/// Polls a non-blocking socket for a bounded time. Returns false if nothing
/// arrived, which the multicast cases treat as "this environment cannot do
/// it" rather than as a defect.
bool wait_for_datagram(UdpSocket& socket, std::vector<std::uint8_t>& buffer,
                       av::eth::ReceiveResult& out) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        out = socket.receive(buffer);
        if (out.status == SocketStatus::Ok) {
            return true;
        }
        if (out.status == SocketStatus::Error) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return false;
}

}  // namespace

TEST(EthIsMulticast, RecognisesTheClassDRange) {
    EXPECT_TRUE(is_multicast("224.0.0.1"));
    EXPECT_TRUE(is_multicast("239.10.0.1"));
    EXPECT_TRUE(is_multicast("239.255.255.255"));
    // The boundaries either side of 224.0.0.0/4.
    EXPECT_FALSE(is_multicast("223.255.255.255"));
    EXPECT_FALSE(is_multicast("240.0.0.1"));
    EXPECT_FALSE(is_multicast("192.168.1.10"));
    EXPECT_FALSE(is_multicast(kLoopback));
}

TEST(EthIsMulticast, RejectsThingsThatAreNotAddresses) {
    EXPECT_FALSE(is_multicast(""));
    EXPECT_FALSE(is_multicast("239.10.0"));
    EXPECT_FALSE(is_multicast("239.10.0.256"));
    EXPECT_FALSE(is_multicast("vcan0"));
}

TEST(EthUdpSocket, OpensAndAcceptsTheOptionsAVehicleNetworkUses) {
    auto socket = UdpSocket::open();
    ASSERT_TRUE(socket.has_value());

    EXPECT_GE(socket->fd(), 0);
    EXPECT_TRUE(socket->set_non_blocking());
    // TTL 1: the datagram reaches the local link and no further.
    EXPECT_TRUE(socket->set_multicast_ttl(1));
    EXPECT_TRUE(socket->set_multicast_loopback(true));
    // 46 is Expedited Forwarding, the code point control traffic usually asks
    // for. Setting it always succeeds; whether any switch honours it does not
    // depend on this call.
    EXPECT_TRUE(socket->set_dscp(46));
}

TEST(EthUdpSocket, RefusesOptionsOutsideTheirRange) {
    auto socket = UdpSocket::open();
    ASSERT_TRUE(socket.has_value());

    EXPECT_FALSE(socket->set_dscp(64));                    // DSCP is 6 bits
    EXPECT_FALSE(socket->set_multicast_ttl(256));          // TTL is 8 bits
    EXPECT_FALSE(socket->join_multicast("192.168.1.1"));   // not in 224.0.0.0/4
    EXPECT_FALSE(socket->join_multicast("not-an-address"));
}

TEST(EthUdpSocket, WouldBlockRatherThanWaitWhenTheQueueIsEmpty) {
    auto socket = UdpSocket::open();
    ASSERT_TRUE(socket.has_value());
    ASSERT_TRUE(socket->set_non_blocking());
    if (!socket->bind_any(kPort)) {
        GTEST_SKIP() << "udp port " << kPort << " is already in use";
    }

    std::vector<std::uint8_t> buffer;
    const auto result = socket->receive(buffer);
    EXPECT_EQ(result.status, SocketStatus::WouldBlock);
    EXPECT_EQ(result.size, 0U);
}

TEST(EthUdpSocket, UnicastRoundTripCarriesTheBytesAndTheSender) {
    auto receiver = UdpSocket::open();
    auto sender = UdpSocket::open();
    ASSERT_TRUE(receiver.has_value());
    ASSERT_TRUE(sender.has_value());
    ASSERT_TRUE(receiver->set_non_blocking());
    if (!receiver->bind_any(kPort)) {
        GTEST_SKIP() << "udp port " << kPort << " is already in use";
    }

    const std::array<std::uint8_t, 4> payload{0xDE, 0xAD, 0xBE, 0xEF};
    ASSERT_EQ(sender->send_to(Endpoint{kLoopback, kPort}, payload.data(), payload.size()),
              SocketStatus::Ok);

    std::vector<std::uint8_t> buffer;
    av::eth::ReceiveResult result;
    ASSERT_TRUE(wait_for_datagram(*receiver, buffer, result));

    // One recvfrom() returns exactly one send_to(): never half, never two.
    ASSERT_EQ(result.size, payload.size());
    EXPECT_EQ(buffer[0], 0xDE);
    EXPECT_EQ(buffer[1], 0xAD);
    EXPECT_EQ(buffer[2], 0xBE);
    EXPECT_EQ(buffer[3], 0xEF);

    // UDP has no connection, so the source is reported per datagram.
    EXPECT_EQ(result.from.address, std::string{kLoopback});
    EXPECT_NE(result.from.port, 0U);
}

TEST(EthUdpSocket, RefusesADatagramThatWouldFragment) {
    auto socket = UdpSocket::open();
    ASSERT_TRUE(socket.has_value());

    const std::vector<std::uint8_t> oversized(kMaxDatagram + 1, 0U);
    EXPECT_EQ(socket->send_to(Endpoint{kLoopback, kPort}, oversized.data(), oversized.size()),
              SocketStatus::Error);

    // One byte less is the largest datagram that still fits a 1500-byte frame.
    const std::vector<std::uint8_t> largest(kMaxDatagram, 0U);
    EXPECT_NE(socket->send_to(Endpoint{kLoopback, kPort}, largest.data(), largest.size()),
              SocketStatus::Error);
}

TEST(EthUdpSocket, MulticastDeliversToAGroupMember) {
    auto receiver = UdpSocket::open();
    auto sender = UdpSocket::open();
    ASSERT_TRUE(receiver.has_value());
    ASSERT_TRUE(sender.has_value());
    ASSERT_TRUE(receiver->set_non_blocking());

    if (!receiver->bind_any(kPort)) {
        GTEST_SKIP() << "udp port " << kPort << " is already in use";
    }
    if (!receiver->join_multicast(kGroup, kLoopback)) {
        GTEST_SKIP() << "no multicast-capable interface at " << kLoopback;
    }
    if (!sender->set_multicast_interface(kLoopback) || !sender->set_multicast_loopback(true) ||
        !sender->set_multicast_ttl(1)) {
        GTEST_SKIP() << "loopback multicast could not be configured here";
    }

    const std::array<std::uint8_t, 3> payload{0x01, 0x02, 0x03};
    if (sender->send_to(Endpoint{kGroup, kPort}, payload.data(), payload.size()) !=
        SocketStatus::Ok) {
        GTEST_SKIP() << "the host refused to send to " << kGroup;
    }

    std::vector<std::uint8_t> buffer;
    av::eth::ReceiveResult result;
    if (!wait_for_datagram(*receiver, buffer, result)) {
        GTEST_SKIP() << "multicast was sent but not delivered on this host";
    }

    ASSERT_EQ(result.size, payload.size());
    EXPECT_EQ(buffer[0], 0x01);
    EXPECT_EQ(buffer[2], 0x03);
}
