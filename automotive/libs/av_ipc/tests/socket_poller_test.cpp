#include <gtest/gtest.h>

#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "av/ipc/poller.hpp"
#include "av/ipc/unique_fd.hpp"
#include "av/ipc/unix_socket.hpp"

namespace {

using av::ipc::IoStatus;
using av::ipc::Poller;
using av::ipc::UniqueFd;
using av::ipc::UnixSocket;

/// Socket paths must be unique per test: ctest may run these concurrently with
/// other builds of the same tree, and a shared path would make two unrelated
/// runs fight over one endpoint.
std::string unique_path(const char* tag) {
    return "/tmp/av-ipc-" + std::to_string(::getpid()) + "-" + tag + ".sock";
}

/// Brings up a connected listener/client pair, the way every test below starts.
struct Pair {
    UnixSocket listener;
    UnixSocket client;
    UnixSocket server;
};

std::optional<Pair> make_pair(const char* tag) {
    const std::string path = unique_path(tag);

    auto listener = UnixSocket::listen(path);
    if (!listener) {
        return std::nullopt;
    }
    auto client = UnixSocket::connect(path);
    if (!client) {
        return std::nullopt;
    }
    auto server = listener->accept();
    if (!server) {
        return std::nullopt;
    }
    return Pair{std::move(*listener), std::move(*client), std::move(*server)};
}

// --- UniqueFd --------------------------------------------------------------

TEST(UniqueFd, DefaultIsInvalid) {
    const UniqueFd fd;
    EXPECT_FALSE(fd.valid());
    EXPECT_EQ(fd.get(), -1);
}

TEST(UniqueFd, MoveTransfersOwnership) {
    UniqueFd first{::dup(STDIN_FILENO)};
    ASSERT_TRUE(first.valid());
    const int raw = first.get();

    const UniqueFd second{std::move(first)};
    EXPECT_EQ(second.get(), raw);
    // Inspecting a moved-from object is exactly what this test is for: the
    // move constructor promises to leave it invalid, and that promise needs
    // checking. Reading it is defined; only *using* it would not be.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(first.valid()) << "the moved-from fd must not still own the descriptor";
}

TEST(UniqueFd, ReleaseGivesUpOwnershipWithoutClosing) {
    UniqueFd fd{::dup(STDIN_FILENO)};
    ASSERT_TRUE(fd.valid());

    const int raw = fd.release();
    EXPECT_FALSE(fd.valid());
    // Still open, because release() did not close it.
    EXPECT_EQ(::close(raw), 0);
}

// --- UnixSocket ------------------------------------------------------------

TEST(UnixSocket, RejectsAPathLongerThanSunPath) {
    const std::string too_long(200, 'x');
    EXPECT_FALSE(UnixSocket::listen("/tmp/" + too_long).has_value())
        << "sun_path is 108 bytes; silently truncating would bind the wrong path";
}

TEST(UnixSocket, ConnectingToNothingFails) {
    EXPECT_FALSE(UnixSocket::connect(unique_path("absent")).has_value());
}

TEST(UnixSocket, DeliversAMessage) {
    auto pair = make_pair("deliver");
    ASSERT_TRUE(pair.has_value());

    const std::array<std::uint8_t, 4> sent{1, 2, 3, 4};
    ASSERT_EQ(pair->client.send(sent.data(), sent.size()), IoStatus::Ok);

    std::array<std::uint8_t, 64> buffer{};
    const auto result = pair->server.receive(buffer.data(), buffer.size());
    EXPECT_EQ(result.status, IoStatus::Ok);
    ASSERT_EQ(result.bytes, sent.size());
    EXPECT_TRUE(std::equal(sent.begin(), sent.end(), buffer.begin()));
}

TEST(UnixSocket, PreservesMessageBoundaries) {
    // The whole reason for SOCK_SEQPACKET. Over SOCK_STREAM these three sends
    // could arrive as one 9-byte read, and the receiver would need its own
    // framing to split them again.
    auto pair = make_pair("boundaries");
    ASSERT_TRUE(pair.has_value());

    const std::array<std::uint8_t, 3> a{1, 1, 1};
    const std::array<std::uint8_t, 2> b{2, 2};
    const std::array<std::uint8_t, 4> c{3, 3, 3, 3};
    ASSERT_EQ(pair->client.send(a.data(), a.size()), IoStatus::Ok);
    ASSERT_EQ(pair->client.send(b.data(), b.size()), IoStatus::Ok);
    ASSERT_EQ(pair->client.send(c.data(), c.size()), IoStatus::Ok);

    std::array<std::uint8_t, 64> buffer{};
    EXPECT_EQ(pair->server.receive(buffer.data(), buffer.size()).bytes, 3U);
    EXPECT_EQ(pair->server.receive(buffer.data(), buffer.size()).bytes, 2U);
    EXPECT_EQ(pair->server.receive(buffer.data(), buffer.size()).bytes, 4U);
}

TEST(UnixSocket, TruncatesAMessageLargerThanTheBuffer) {
    auto pair = make_pair("truncate");
    ASSERT_TRUE(pair.has_value());

    const std::array<std::uint8_t, 32> sent{};
    ASSERT_EQ(pair->client.send(sent.data(), sent.size()), IoStatus::Ok);

    std::array<std::uint8_t, 8> small{};
    const auto result = pair->server.receive(small.data(), small.size());
    EXPECT_EQ(result.status, IoStatus::Ok);
    EXPECT_EQ(result.bytes, small.size()) << "the rest of the datagram is discarded, not queued";

    // Proof that the remainder is gone rather than waiting: the next receive
    // has nothing to return.
    ASSERT_TRUE(pair->server.set_non_blocking());
    EXPECT_EQ(pair->server.receive(small.data(), small.size()).status, IoStatus::WouldBlock);
}

TEST(UnixSocket, ReportsPeerClose) {
    auto pair = make_pair("peerclose");
    ASSERT_TRUE(pair.has_value());

    {
        UnixSocket doomed = std::move(pair->client);
        static_cast<void>(doomed.send("x", 1));
    }  // client destroyed -> fd closed

    std::array<std::uint8_t, 8> buffer{};
    EXPECT_EQ(pair->server.receive(buffer.data(), buffer.size()).status, IoStatus::Ok);
    EXPECT_EQ(pair->server.receive(buffer.data(), buffer.size()).status, IoStatus::PeerClosed);
}

TEST(UnixSocket, NonBlockingReceiveReportsWouldBlockNotError) {
    auto pair = make_pair("wouldblock");
    ASSERT_TRUE(pair.has_value());
    ASSERT_TRUE(pair->server.set_non_blocking());

    std::array<std::uint8_t, 8> buffer{};
    const auto result = pair->server.receive(buffer.data(), buffer.size());
    EXPECT_EQ(result.status, IoStatus::WouldBlock)
        << "an empty non-blocking socket is normal, not broken";
}

TEST(UnixSocket, ListenerRemovesItsSocketFile) {
    const std::string path = unique_path("unlink");
    {
        auto listener = UnixSocket::listen(path);
        ASSERT_TRUE(listener.has_value());
        EXPECT_EQ(::access(path.c_str(), F_OK), 0);
    }
    EXPECT_NE(::access(path.c_str(), F_OK), 0)
        << "closing a socket does not unlink its path; the owner must";
}

// --- Poller ----------------------------------------------------------------

TEST(Poller, ReportsNothingWhenNothingIsReady) {
    auto poller = Poller::create();
    ASSERT_TRUE(poller.has_value());

    auto pair = make_pair("idle");
    ASSERT_TRUE(pair.has_value());
    ASSERT_TRUE(poller->watch_readable(pair->server.fd()));

    EXPECT_TRUE(poller->wait(std::chrono::milliseconds{10}).empty());
}

TEST(Poller, WakesOnAReadableSocket) {
    auto poller = Poller::create();
    ASSERT_TRUE(poller.has_value());

    auto pair = make_pair("readable");
    ASSERT_TRUE(pair.has_value());
    ASSERT_TRUE(poller->watch_readable(pair->server.fd()));

    ASSERT_EQ(pair->client.send("hi", 2), IoStatus::Ok);

    const auto& events = poller->wait(std::chrono::milliseconds{500});
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].fd, pair->server.fd());
    EXPECT_TRUE(events[0].readable);
    EXPECT_FALSE(events[0].hangup);
}

TEST(Poller, WatchesSeveralDescriptorsAtOnce) {
    // This is the reason epoll exists: one thread, two peers, neither of them
    // able to starve the other.
    auto poller = Poller::create();
    ASSERT_TRUE(poller.has_value());

    auto first = make_pair("multi-a");
    auto second = make_pair("multi-b");
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(poller->watch_readable(first->server.fd()));
    ASSERT_TRUE(poller->watch_readable(second->server.fd()));

    // Only the second peer speaks. The first must not be reported.
    ASSERT_EQ(second->client.send("x", 1), IoStatus::Ok);

    const auto& events = poller->wait(std::chrono::milliseconds{500});
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].fd, second->server.fd());
}

TEST(Poller, ReportsHangupWhenThePeerDisconnects) {
    auto poller = Poller::create();
    ASSERT_TRUE(poller.has_value());

    auto pair = make_pair("hangup");
    ASSERT_TRUE(pair.has_value());
    ASSERT_TRUE(poller->watch_readable(pair->server.fd()));

    { const UnixSocket doomed = std::move(pair->client); }  // close the peer

    const auto& events = poller->wait(std::chrono::milliseconds{500});
    ASSERT_EQ(events.size(), 1U);
    EXPECT_TRUE(events[0].hangup)
        << "without EPOLLRDHUP a dead peer is only noticed on the next read";
}

TEST(Poller, UnwatchStopsReporting) {
    auto poller = Poller::create();
    ASSERT_TRUE(poller.has_value());

    auto pair = make_pair("unwatch");
    ASSERT_TRUE(pair.has_value());
    ASSERT_TRUE(poller->watch_readable(pair->server.fd()));
    ASSERT_TRUE(poller->unwatch(pair->server.fd()));

    ASSERT_EQ(pair->client.send("x", 1), IoStatus::Ok);
    EXPECT_TRUE(poller->wait(std::chrono::milliseconds{50}).empty());
}

TEST(Poller, UnwatchingAnUnknownFdIsNotAnError) {
    auto poller = Poller::create();
    ASSERT_TRUE(poller.has_value());
    auto pair = make_pair("unknown");
    ASSERT_TRUE(pair.has_value());

    // Closing an fd removes it from every epoll set, so a later DEL sees
    // ENOENT. Shutdown paths hit this constantly and must not treat it as
    // a failure.
    EXPECT_TRUE(poller->unwatch(pair->server.fd()));
}

}  // namespace
