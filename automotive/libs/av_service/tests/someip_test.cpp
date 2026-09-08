// The SOME/IP header, asserted byte by byte.
//
// A round-trip test alone would pass with the Length field computed wrongly,
// as long as both sides were wrong the same way -- and that is exactly the bug
// that survives until a foreign stack appears. So the central case here pins
// the *bytes*, not just the round trip.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "av/service/someip.hpp"

namespace {

using av::service::deserialize;
using av::service::Header;
using av::service::is_event;
using av::service::kHeaderSize;
using av::service::MessageType;
using av::service::PayloadReader;
using av::service::PayloadWriter;
using av::service::ReturnCode;
using av::service::serialize;
using av::service::SessionCounter;

Header sample_header() {
    Header header;
    header.service_id = 0x1234;
    header.method_id = 0x8001;  // bit 15 set: an event
    header.client_id = 0xABCD;
    header.session_id = 0x0007;
    header.interface_version = 0x02;
    header.type = MessageType::Notification;
    header.code = ReturnCode::Ok;
    return header;
}

}  // namespace

TEST(SomeIpHeader, EncodesEveryFieldBigEndianAtItsFixedOffset) {
    PayloadWriter payload;
    payload.put_f32(87.5F);

    std::vector<std::uint8_t> wire;
    ASSERT_TRUE(serialize(sample_header(), payload.bytes(), wire));
    ASSERT_EQ(wire.size(), kHeaderSize + 4U);

    const std::vector<std::uint8_t> expected{
        0x12, 0x34,              // service id
        0x80, 0x01,              // method id (event)
        0x00, 0x00, 0x00, 0x0C,  // length = 8 + 4 payload  <- NOT 20, NOT 4
        0xAB, 0xCD,              // client id
        0x00, 0x07,              // session id
        0x01,                    // protocol version
        0x02,                    // interface version
        0x02,                    // message type: Notification
        0x00,                    // return code: Ok
        0x42, 0xAF, 0x00, 0x00,  // 87.5f, IEEE 754 big-endian
    };
    EXPECT_EQ(wire, expected);
}

TEST(SomeIpHeader, LengthCountsFromRequestIdNotFromTheStart) {
    std::vector<std::uint8_t> wire;
    ASSERT_TRUE(serialize(sample_header(), {}, wire));

    // An empty payload still declares 8: the Request ID and the four
    // version/type/code bytes are inside the count.
    EXPECT_EQ(wire.size(), kHeaderSize);
    EXPECT_EQ(wire[4], 0x00);
    EXPECT_EQ(wire[5], 0x00);
    EXPECT_EQ(wire[6], 0x00);
    EXPECT_EQ(wire[7], 0x08);
}

TEST(SomeIpHeader, RoundTripsThroughDeserialize) {
    PayloadWriter payload;
    payload.put_u8(0x11);
    payload.put_u16(0x2233);
    payload.put_u32(0x4455'6677);
    payload.put_f32(-1.5F);

    std::vector<std::uint8_t> wire;
    ASSERT_TRUE(serialize(sample_header(), payload.bytes(), wire));

    const auto message = deserialize(wire.data(), wire.size());
    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(message->header.service_id, 0x1234);
    EXPECT_EQ(message->header.method_id, 0x8001);
    EXPECT_EQ(message->header.client_id, 0xABCD);
    EXPECT_EQ(message->header.session_id, 0x0007);
    EXPECT_EQ(message->header.interface_version, 0x02);
    EXPECT_EQ(message->header.type, MessageType::Notification);
    EXPECT_EQ(message->header.code, ReturnCode::Ok);

    PayloadReader reader{message->payload.data(), message->payload.size()};
    EXPECT_EQ(reader.get_u8(), 0x11);
    EXPECT_EQ(reader.get_u16(), 0x2233);
    EXPECT_EQ(reader.get_u32(), 0x4455'6677U);
    EXPECT_EQ(reader.get_f32(), -1.5F);
    EXPECT_TRUE(reader.exhausted());
}

TEST(SomeIpHeader, SeparatesMethodsFromEventsByBitFifteen) {
    EXPECT_FALSE(is_event(0x0000));
    EXPECT_FALSE(is_event(0x0001));  // GetSpeed()
    EXPECT_FALSE(is_event(0x7FFF));
    EXPECT_TRUE(is_event(0x8000));  // OnSpeedChanged
    EXPECT_TRUE(is_event(0x8001));
    EXPECT_TRUE(is_event(0xFFFF));
}

TEST(SomeIpHeader, RejectsWhatItCannotSafelyInterpret) {
    std::vector<std::uint8_t> good;
    ASSERT_TRUE(serialize(sample_header(), {0xAA, 0xBB}, good));

    EXPECT_FALSE(deserialize(nullptr, 32).has_value());
    EXPECT_FALSE(deserialize(good.data(), kHeaderSize - 1).has_value());

    // A peer that computed Length over the whole message instead of from the
    // Request ID. This is the bug the header comment warns about, caught here
    // rather than as "the payload looks like noise" three layers up.
    auto whole_message_length = good;
    whole_message_length[7] = static_cast<std::uint8_t>(good.size());
    EXPECT_FALSE(deserialize(whole_message_length.data(), whole_message_length.size()).has_value());

    auto truncated_body = good;
    truncated_body.pop_back();
    EXPECT_FALSE(deserialize(truncated_body.data(), truncated_body.size()).has_value());

    auto future_protocol = good;
    future_protocol[12] = 0x02;
    EXPECT_FALSE(deserialize(future_protocol.data(), future_protocol.size()).has_value());

    // 0x20 is a SOME/IP-TP segment: a real message type, but not a whole
    // message -- accepting it would hand a fragment to a caller as if complete.
    auto tp_segment = good;
    tp_segment[14] = 0x20;
    EXPECT_FALSE(deserialize(tp_segment.data(), tp_segment.size()).has_value());

    // A Length field smaller than the fields it must cover.
    auto impossible_length = good;
    impossible_length[7] = 0x04;
    EXPECT_FALSE(deserialize(impossible_length.data(), impossible_length.size()).has_value());
}

TEST(SomeIpSession, SkipsZeroWhenItWraps) {
    SessionCounter counter;
    EXPECT_EQ(counter.next(), 1);
    EXPECT_EQ(counter.next(), 2);

    SessionCounter wrapping;
    constexpr int kToTheTop = std::numeric_limits<std::uint16_t>::max();
    std::uint16_t last = 0;
    for (int i = 0; i < kToTheTop; ++i) {
        last = wrapping.next();
    }
    EXPECT_EQ(last, std::numeric_limits<std::uint16_t>::max());

    // 0 means "this endpoint does not track sessions", so the counter must
    // never emit it -- announcing that by accident would tell every peer to
    // stop expecting ordering.
    EXPECT_EQ(wrapping.next(), 1);
}

TEST(SomeIpPayload, ReaderRefusesToRunOffTheEnd) {
    PayloadWriter writer;
    writer.put_u16(0x0102);

    PayloadReader reader{writer.bytes().data(), writer.bytes().size()};
    EXPECT_EQ(reader.remaining(), 2U);
    EXPECT_FALSE(reader.get_u32().has_value());  // not enough left
    EXPECT_EQ(reader.remaining(), 2U);           // and nothing was consumed
    EXPECT_EQ(reader.get_u16(), 0x0102);
    EXPECT_TRUE(reader.exhausted());
    EXPECT_FALSE(reader.get_u8().has_value());
}
