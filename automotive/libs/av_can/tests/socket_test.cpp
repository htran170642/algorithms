#include "av/can/socket.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "av/can/frame.hpp"

namespace {

using av::can::CanFilter;
using av::can::CanFrame;
using av::can::CanSocket;
using av::can::FrameKind;
using av::can::interface_index;
using av::can::ReceiveResult;
using av::can::SocketStatus;

constexpr const char* kInterface = "vcan0";

/// vcan is not persistent, so a fresh machine (or a reboot) has no vcan0. A
/// skipped test says so out loud; a failing one would just look like a broken
/// build.
bool interface_available() { return interface_index(kInterface).has_value(); }

#define REQUIRE_VCAN()                                                            \
    do {                                                                          \
        if (!interface_available()) {                                             \
            GTEST_SKIP() << "vcan0 is absent; run: sudo ./scripts/setup_vcan.sh"; \
        }                                                                         \
    } while (false)

/// A sender and a receiver on the same bus.
///
/// Two sockets, not one: SocketCAN does not echo a frame back to the socket
/// that sent it unless CAN_RAW_RECV_OWN_MSGS is set. Local loopback delivers
/// to *other* sockets on the interface, which is exactly a second ECU.
struct Pair {
    CanSocket tx;
    CanSocket rx;
};

std::optional<Pair> make_pair() {
    auto tx = CanSocket::open(kInterface);
    auto rx = CanSocket::open(kInterface);
    if (!tx || !rx) {
        return std::nullopt;
    }
    if (!rx->set_non_blocking()) {
        return std::nullopt;
    }
    return Pair{std::move(*tx), std::move(*rx)};
}

/// Polls for up to ~200 ms. The loopback is in-kernel and effectively
/// immediate, but a bounded wait keeps a scheduling hiccup from flaking CI.
ReceiveResult wait_for_frame(CanSocket& socket) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        ReceiveResult result = socket.receive();
        if (result.status != SocketStatus::WouldBlock) {
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return ReceiveResult{};
}

CanFrame make_frame(std::uint32_t id, std::uint8_t length, bool extended = false) {
    CanFrame frame{};
    frame.id = id;
    frame.length = length;
    frame.extended = extended;
    for (std::uint8_t i = 0; i < length; ++i) {
        frame.data[i] = static_cast<std::uint8_t>(0x10U + i);
    }
    return frame;
}

// --- Name resolution, no interface required --------------------------------

TEST(InterfaceIndex, AnEmptyNameMeansEveryInterface) {
    const auto index = interface_index("");
    ASSERT_TRUE(index.has_value());
    EXPECT_EQ(*index, 0U) << "ifindex 0 is what `candump any` binds to";
}

TEST(InterfaceIndex, RejectsAnUnknownName) {
    EXPECT_FALSE(interface_index("av-no-such-if").has_value());
}

TEST(InterfaceIndex, RejectsANameLongerThanIfnamsiz) {
    EXPECT_FALSE(interface_index(std::string(64, 'x')).has_value());
}

TEST(CanSocket, RefusesToOpenAnUnknownInterface) {
    EXPECT_FALSE(CanSocket::open("av-no-such-if").has_value());
}

// --- Round trips over vcan0 ------------------------------------------------

TEST(CanSocket, DeliversAFrameToAnotherSocketOnTheSameBus) {
    REQUIRE_VCAN();
    auto pair = make_pair();
    ASSERT_TRUE(pair.has_value());

    const CanFrame sent = make_frame(0x123U, 8U);
    ASSERT_EQ(pair->tx.send(sent), SocketStatus::Ok);

    const ReceiveResult got = wait_for_frame(pair->rx);
    ASSERT_EQ(got.status, SocketStatus::Ok);
    ASSERT_EQ(got.kind, FrameKind::Data);
    EXPECT_EQ(got.frame.id, 0x123U);
    EXPECT_EQ(got.frame.length, 8U);
    EXPECT_FALSE(got.frame.extended);
    EXPECT_EQ(got.frame.data[0], 0x10U);
    EXPECT_EQ(got.frame.data[7], 0x17U);
}

TEST(CanSocket, StripsTheFlagBitsFromAnExtendedIdentifier) {
    // The kernel packs CAN_EFF_FLAG into the top of can_id. A receiver that
    // forgets to mask it reports 0x81ABCDEF instead of 0x1ABCDEF, and every
    // DBC lookup misses.
    REQUIRE_VCAN();
    auto pair = make_pair();
    ASSERT_TRUE(pair.has_value());

    const CanFrame sent = make_frame(0x1ABCDEFU, 4U, /*extended=*/true);
    ASSERT_EQ(pair->tx.send(sent), SocketStatus::Ok);

    const ReceiveResult got = wait_for_frame(pair->rx);
    ASSERT_EQ(got.status, SocketStatus::Ok);
    EXPECT_TRUE(got.frame.extended);
    EXPECT_EQ(got.frame.id, 0x1ABCDEFU) << "no flag bits left in the identifier";
}

TEST(CanSocket, CarriesAnEmptyPayload) {
    REQUIRE_VCAN();
    auto pair = make_pair();
    ASSERT_TRUE(pair.has_value());

    ASSERT_EQ(pair->tx.send(make_frame(0x001U, 0U)), SocketStatus::Ok);

    const ReceiveResult got = wait_for_frame(pair->rx);
    ASSERT_EQ(got.status, SocketStatus::Ok);
    EXPECT_EQ(got.frame.length, 0U) << "DLC 0 is a legal frame, not an error";
}

TEST(CanSocket, AnEmptySocketReportsWouldBlockNotError) {
    REQUIRE_VCAN();
    auto pair = make_pair();
    ASSERT_TRUE(pair.has_value());

    EXPECT_EQ(pair->rx.receive().status, SocketStatus::WouldBlock)
        << "an idle non-blocking CAN socket is normal, not broken";
}

// --- Kernel-side filtering -------------------------------------------------

TEST(CanSocket, DeliversOnlyTheFilteredIdentifiers) {
    REQUIRE_VCAN();
    auto pair = make_pair();
    ASSERT_TRUE(pair.has_value());

    ASSERT_TRUE(pair->rx.set_filters({CanFilter{0x200U, av::can::kStandardIdMask, false}}));

    ASSERT_EQ(pair->tx.send(make_frame(0x100U, 1U)), SocketStatus::Ok);
    ASSERT_EQ(pair->tx.send(make_frame(0x200U, 2U)), SocketStatus::Ok);

    const ReceiveResult got = wait_for_frame(pair->rx);
    ASSERT_EQ(got.status, SocketStatus::Ok);
    EXPECT_EQ(got.frame.id, 0x200U) << "0x100 was dropped by the kernel, never copied to us";
}

TEST(CanSocket, AnEmptyFilterSetDeliversNothing) {
    // The trap: an empty filter list is not "accept everything". A receiver
    // that clears its filters to "reset" them goes permanently silent.
    REQUIRE_VCAN();
    auto pair = make_pair();
    ASSERT_TRUE(pair.has_value());

    ASSERT_TRUE(pair->rx.set_filters({}));
    ASSERT_EQ(pair->tx.send(make_frame(0x123U, 8U)), SocketStatus::Ok);

    EXPECT_EQ(pair->rx.receive().status, SocketStatus::WouldBlock);
}

// --- CAN-FD ----------------------------------------------------------------

TEST(CanSocket, RoundTripsACanFdFrameWhenTheInterfaceAllowsIt) {
    REQUIRE_VCAN();
    auto pair = make_pair();
    ASSERT_TRUE(pair.has_value());

    if (!pair->tx.enable_fd() || !pair->rx.enable_fd()) {
        GTEST_SKIP() << "interface MTU is 16: classic CAN only";
    }

    CanFrame sent = make_frame(0x321U, 48U);
    sent.fd = true;
    sent.brs = true;
    ASSERT_EQ(pair->tx.send(sent), SocketStatus::Ok);

    const ReceiveResult got = wait_for_frame(pair->rx);
    ASSERT_EQ(got.status, SocketStatus::Ok);
    EXPECT_TRUE(got.frame.fd);
    EXPECT_TRUE(got.frame.brs);
    EXPECT_EQ(got.frame.length, 48U) << "48 is a legal FD length; 47 is not";
}

TEST(CanSocket, AClassicSocketNeverSeesAnFdFrame) {
    // The week-5 silent fault. The kernel drops the frame on the way in: no
    // error, no counter, no log line. The receiver simply never hears from
    // that ECU, and nothing anywhere says why.
    REQUIRE_VCAN();
    auto pair = make_pair();
    ASSERT_TRUE(pair.has_value());

    if (!pair->tx.enable_fd()) {
        GTEST_SKIP() << "interface MTU is 16: classic CAN only";
    }
    // pair->rx deliberately stays classic.

    CanFrame sent = make_frame(0x321U, 16U);
    sent.fd = true;
    ASSERT_EQ(pair->tx.send(sent), SocketStatus::Ok);

    EXPECT_EQ(pair->rx.receive().status, SocketStatus::WouldBlock)
        << "silently dropped -- this is the fault to remember";
}

// --- Error frames ----------------------------------------------------------

TEST(CanSocket, AcceptsTheErrorFrameFilter) {
    // vcan is a loopback with no controller, so it never generates a real bus
    // error. What can be asserted here is that the socket option is accepted;
    // the decode path needs hardware to exercise end to end.
    REQUIRE_VCAN();
    auto socket = CanSocket::open(kInterface);
    ASSERT_TRUE(socket.has_value());
    EXPECT_TRUE(socket->enable_error_frames());
}

}  // namespace
