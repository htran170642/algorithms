#include "av/can/frame.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using av::can::CanFrame;
using av::can::dlc_to_length;
using av::can::is_valid;
using av::can::length_to_dlc;

TEST(Dlc, ClassicCodesAreTheByteCount) {
    for (std::uint8_t dlc = 0U; dlc <= 8U; ++dlc) {
        EXPECT_EQ(dlc_to_length(dlc, /*fd=*/false), dlc);
        EXPECT_EQ(dlc_to_length(dlc, /*fd=*/true), dlc);
    }
}

TEST(Dlc, ClassicSaturatesAtEightBytes) {
    // A classic controller ignores the top of the code; it always sends 8.
    EXPECT_EQ(dlc_to_length(9U, /*fd=*/false), 8U);
    EXPECT_EQ(dlc_to_length(15U, /*fd=*/false), 8U);
}

TEST(Dlc, FdCodesJumpToTheLongPayloads) {
    EXPECT_EQ(dlc_to_length(9U, /*fd=*/true), 12U);
    EXPECT_EQ(dlc_to_length(10U, /*fd=*/true), 16U);
    EXPECT_EQ(dlc_to_length(11U, /*fd=*/true), 20U);
    EXPECT_EQ(dlc_to_length(12U, /*fd=*/true), 24U);
    EXPECT_EQ(dlc_to_length(13U, /*fd=*/true), 32U);
    EXPECT_EQ(dlc_to_length(14U, /*fd=*/true), 48U);
    EXPECT_EQ(dlc_to_length(15U, /*fd=*/true), 64U);
}

TEST(Dlc, RoundTripsForEveryEncodableLength) {
    for (std::uint8_t dlc = 0U; dlc <= 15U; ++dlc) {
        const std::uint8_t length = dlc_to_length(dlc, /*fd=*/true);
        const auto back = length_to_dlc(length, /*fd=*/true);
        ASSERT_TRUE(back.has_value()) << "length=" << unsigned{length};
        EXPECT_EQ(dlc_to_length(back.value(), /*fd=*/true), length);
    }
}

TEST(Dlc, LengthsBetweenTheFdStepsCannotBeSent) {
    // This is the practical consequence of the sparse table: a 13-byte
    // CAN-FD payload does not exist. A real stack pads to 16 and the DBC
    // declares the message as 16.
    EXPECT_FALSE(length_to_dlc(13U, /*fd=*/true).has_value());
    EXPECT_FALSE(length_to_dlc(9U, /*fd=*/true).has_value());
    EXPECT_FALSE(length_to_dlc(63U, /*fd=*/true).has_value());
    EXPECT_FALSE(length_to_dlc(65U, /*fd=*/true).has_value());
}

TEST(Dlc, ClassicCannotSendMoreThanEight) {
    EXPECT_FALSE(length_to_dlc(12U, /*fd=*/false).has_value());
    EXPECT_FALSE(length_to_dlc(64U, /*fd=*/false).has_value());
}

TEST(FrameValidity, AcceptsAnOrdinaryClassicFrame) {
    CanFrame frame{};
    frame.id = 0x100U;
    frame.length = 8U;
    EXPECT_TRUE(is_valid(frame));
}

TEST(FrameValidity, RejectsAnIdWiderThanItsFormat) {
    CanFrame frame{};
    frame.length = 8U;

    frame.id = 0x800U;  // needs 12 bits, standard ids have 11
    EXPECT_FALSE(is_valid(frame));

    frame.extended = true;  // the same id is fine once the frame says so
    EXPECT_TRUE(is_valid(frame));

    frame.id = 0x2000'0000U;  // needs 30 bits, extended ids have 29
    EXPECT_FALSE(is_valid(frame));
}

TEST(FrameValidity, RejectsBitRateSwitchOnAClassicFrame) {
    CanFrame frame{};
    frame.id = 0x100U;
    frame.length = 8U;
    frame.brs = true;
    EXPECT_FALSE(is_valid(frame)) << "BRS only exists in CAN-FD";

    frame.fd = true;
    EXPECT_TRUE(is_valid(frame));
}

TEST(FrameValidity, RejectsALengthTheBusCannotCarry) {
    CanFrame frame{};
    frame.id = 0x100U;
    frame.length = 16U;
    EXPECT_FALSE(is_valid(frame)) << "classic CAN stops at 8 bytes";

    frame.fd = true;
    EXPECT_TRUE(is_valid(frame));

    frame.length = 13U;
    EXPECT_FALSE(is_valid(frame)) << "13 is not an encodable CAN-FD length";
}

}  // namespace
