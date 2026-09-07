// The tunnel format, and the sequence logic that makes it useful.
//
// The round-trip cases are the easy half. The interesting half is what the
// decoder *refuses*, and what SequenceTracker says about a stream of numbers
// that arrive in the wrong order -- including across the 2^32 wrap, which is
// the one case a plain `<` comparison gets wrong and no short run would ever
// notice.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "av/can/frame.hpp"
#include "av/can/tunnel.hpp"

namespace {

using av::can::CanFrame;
using av::can::kMaxFramesPerDatagram;
using av::can::kTunnelHeaderSize;
using av::can::kTunnelRecordSize;
using av::can::PacketOrder;
using av::can::SequenceTracker;
using av::can::tunnel_decode;
using av::can::tunnel_encode;
using av::can::TunnelHeader;

CanFrame make_frame(std::uint32_t id, std::uint8_t length, bool extended = false) {
    CanFrame frame{};
    frame.id = id;
    frame.length = length;
    frame.extended = extended;
    for (std::uint8_t i = 0; i < length; ++i) {
        frame.data[i] = static_cast<std::uint8_t>(0xA0U + i);
    }
    return frame;
}

}  // namespace

TEST(CanTunnel, RoundTripsOneFrame) {
    const std::vector<CanFrame> frames{make_frame(0x100, 8)};
    std::vector<std::uint8_t> buffer;
    ASSERT_TRUE(tunnel_encode(TunnelHeader{42, 1234, 1}, frames, buffer));
    EXPECT_EQ(buffer.size(), kTunnelHeaderSize + kTunnelRecordSize);

    const auto packet = tunnel_decode(buffer.data(), buffer.size());
    ASSERT_TRUE(packet.has_value());
    EXPECT_EQ(packet->header.sequence, 42U);
    EXPECT_EQ(packet->header.timestamp_ms, 1234U);
    ASSERT_EQ(packet->frames.size(), 1U);
    EXPECT_EQ(packet->frames[0].id, 0x100U);
    EXPECT_EQ(packet->frames[0].length, 8U);
    EXPECT_EQ(packet->frames[0].data[0], 0xA0U);
    EXPECT_EQ(packet->frames[0].data[7], 0xA7U);
}

TEST(CanTunnel, RoundTripsAFullBatchWithoutFragmenting) {
    std::vector<CanFrame> frames;
    frames.reserve(kMaxFramesPerDatagram);
    for (std::size_t i = 0; i < kMaxFramesPerDatagram; ++i) {
        frames.push_back(make_frame(static_cast<std::uint32_t>(0x200U + i), 8));
    }

    std::vector<std::uint8_t> buffer;
    ASSERT_TRUE(tunnel_encode(TunnelHeader{1, 0, 0}, frames, buffer));
    // 16 + 64*16 = 1040, under av::eth::kMaxDatagram (1472).
    EXPECT_EQ(buffer.size(), 1040U);
    EXPECT_LT(buffer.size(), 1472U);

    const auto packet = tunnel_decode(buffer.data(), buffer.size());
    ASSERT_TRUE(packet.has_value());
    ASSERT_EQ(packet->frames.size(), kMaxFramesPerDatagram);
    EXPECT_EQ(packet->frames[63].id, 0x23FU);
}

TEST(CanTunnel, PreservesTheExtendedFlag) {
    const std::vector<CanFrame> frames{make_frame(0x1AB'CDEF, 4, true)};
    std::vector<std::uint8_t> buffer;
    ASSERT_TRUE(tunnel_encode(TunnelHeader{0, 0, 0}, frames, buffer));

    const auto packet = tunnel_decode(buffer.data(), buffer.size());
    ASSERT_TRUE(packet.has_value());
    ASSERT_EQ(packet->frames.size(), 1U);
    // Bit 31 carried the flag; the id itself must come back unchanged.
    EXPECT_TRUE(packet->frames[0].extended);
    EXPECT_EQ(packet->frames[0].id, 0x1AB'CDEFU);
    EXPECT_EQ(packet->frames[0].length, 4U);
}

TEST(CanTunnel, EncodeRefusesBatchesItCannotRepresent) {
    std::vector<std::uint8_t> buffer;

    EXPECT_FALSE(tunnel_encode(TunnelHeader{0, 0, 0}, {}, buffer));
    EXPECT_TRUE(buffer.empty());

    const std::vector<CanFrame> too_many(kMaxFramesPerDatagram + 1, make_frame(0x100, 8));
    EXPECT_FALSE(tunnel_encode(TunnelHeader{0, 0, 0}, too_many, buffer));
    EXPECT_TRUE(buffer.empty());

    // A CAN-FD payload does not fit a 16-byte record. Refused, not truncated:
    // 8 of 32 bytes would decode as confident nonsense.
    CanFrame fd_frame = make_frame(0x300, 8);
    fd_frame.fd = true;
    fd_frame.length = 32;
    EXPECT_FALSE(tunnel_encode(TunnelHeader{0, 0, 0}, {fd_frame}, buffer));
    EXPECT_TRUE(buffer.empty());
}

TEST(CanTunnel, DecodeRejectsAnythingThatIsNotOurs) {
    const std::vector<CanFrame> frames{make_frame(0x100, 8)};
    std::vector<std::uint8_t> good;
    ASSERT_TRUE(tunnel_encode(TunnelHeader{7, 0, 1}, frames, good));

    EXPECT_FALSE(tunnel_decode(nullptr, 32).has_value());
    EXPECT_FALSE(tunnel_decode(good.data(), kTunnelHeaderSize - 1).has_value());

    // Somebody else's traffic on the same multicast group.
    auto foreign = good;
    foreign[0] = 'X';
    EXPECT_FALSE(tunnel_decode(foreign.data(), foreign.size()).has_value());

    auto future_version = good;
    future_version[4] = 99;
    EXPECT_FALSE(tunnel_decode(future_version.data(), future_version.size()).has_value());

    // A length that disagrees with the count. Exact, not "at least": a datagram
    // is atomic, so this is malformed, never a partial packet awaiting more.
    EXPECT_FALSE(tunnel_decode(good.data(), good.size() - 1).has_value());

    auto lying_count = good;
    lying_count[5] = 2;  // claims two records, carries one
    EXPECT_FALSE(tunnel_decode(lying_count.data(), lying_count.size()).has_value());

    auto zero_count = good;
    zero_count[5] = 0;
    EXPECT_FALSE(tunnel_decode(zero_count.data(), zero_count.size()).has_value());
}

TEST(CanSequenceTracker, ClassifiesOrderedLostAndReorderedDatagrams) {
    SequenceTracker tracker;

    EXPECT_EQ(tracker.observe(10), PacketOrder::First);
    EXPECT_EQ(tracker.observe(11), PacketOrder::InOrder);

    // 12 and 13 never arrived.
    EXPECT_EQ(tracker.observe(14), PacketOrder::Gap);
    EXPECT_EQ(tracker.lost(), 2U);

    // 13 turns up late. Older than what has already been shown, so it must not
    // be applied -- this is the datagram that walks the speed backwards.
    EXPECT_EQ(tracker.observe(13), PacketOrder::Stale);
    EXPECT_EQ(tracker.observe(14), PacketOrder::Duplicate);
    EXPECT_EQ(tracker.stale(), 2U);

    EXPECT_EQ(tracker.observe(15), PacketOrder::InOrder);
    EXPECT_EQ(tracker.accepted(), 4U);  // 10, 11, 14, 15
    EXPECT_EQ(tracker.lost(), 2U);
}

TEST(CanSequenceTracker, SurvivesTheThirtyTwoBitWrap) {
    SequenceTracker tracker;
    constexpr std::uint32_t kMax = std::numeric_limits<std::uint32_t>::max();

    EXPECT_EQ(tracker.observe(kMax - 1U), PacketOrder::First);
    EXPECT_EQ(tracker.observe(kMax), PacketOrder::InOrder);

    // The counter wraps to 0. A plain `sequence < highest_` would call this
    // stale and reject every datagram from here on, permanently. The signed
    // delta calls it what it is: the next one.
    EXPECT_EQ(tracker.observe(0), PacketOrder::InOrder);
    EXPECT_EQ(tracker.observe(1), PacketOrder::InOrder);

    // And ordering still works on the far side of the wrap.
    EXPECT_EQ(tracker.observe(kMax), PacketOrder::Stale);
    EXPECT_EQ(tracker.accepted(), 4U);
    EXPECT_EQ(tracker.lost(), 0U);
}
