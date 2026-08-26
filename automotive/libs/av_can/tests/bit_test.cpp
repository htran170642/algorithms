#include "av/can/bit.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace {

using av::can::ByteOrder;
using av::can::extract_bits;
using av::can::insert_bits;
using av::can::sign_extend;

const char* order_name(ByteOrder order) {
    return order == ByteOrder::Intel ? "Intel" : "Motorola";
}

// --- Intel -----------------------------------------------------------------

TEST(IntelLayout, SingleByteIsJustTheByte) {
    const std::array<std::uint8_t, 8> data{0xA5};
    EXPECT_EQ(extract_bits(data.data(), data.size(), 0, 8, ByteOrder::Intel), 0xA5U);
}

TEST(IntelLayout, SixteenBitsAreLittleEndian) {
    // Low byte first: this is what makes it "Intel".
    const std::array<std::uint8_t, 8> data{0x34, 0x12};
    EXPECT_EQ(extract_bits(data.data(), data.size(), 0, 16, ByteOrder::Intel), 0x1234U);
}

TEST(IntelLayout, StraddlesAByteBoundary) {
    // Signal at bit 4, 8 bits long: the low nibble of the value comes from the
    // top nibble of byte 0, the high nibble from the bottom nibble of byte 1.
    const std::array<std::uint8_t, 8> data{0xA0, 0x05};
    EXPECT_EQ(extract_bits(data.data(), data.size(), 4, 8, ByteOrder::Intel), 0x5AU);
}

// --- Motorola --------------------------------------------------------------

TEST(MotorolaLayout, SixteenBitsAreBigEndian) {
    // Start bit 7 is byte 0's MSB. The walk covers byte 0 then byte 1.
    const std::array<std::uint8_t, 8> data{0x12, 0x34};
    EXPECT_EQ(extract_bits(data.data(), data.size(), 7, 16, ByteOrder::Motorola), 0x1234U);
}

TEST(MotorolaLayout, StartBitTwentyThreeCoversBytesTwoAndThree) {
    // 23 = byte 2, bit 7. The +15 jump at the byte boundary lands on byte 3.
    const std::array<std::uint8_t, 8> data{0x00, 0x00, 0xAB, 0xCD};
    EXPECT_EQ(extract_bits(data.data(), data.size(), 23, 16, ByteOrder::Motorola), 0xABCDU);
}

TEST(MotorolaLayout, StraddlesAByteBoundary) {
    // Start bit 3, 8 bits: byte 0's low nibble is the value's *high* nibble,
    // then the walk jumps to byte 1's high nibble.
    const std::array<std::uint8_t, 8> data{0x0A, 0x50};
    EXPECT_EQ(extract_bits(data.data(), data.size(), 3, 8, ByteOrder::Motorola), 0xA5U);
}

// --- The two layouts genuinely disagree ------------------------------------

TEST(ByteOrderMatters, SameBytesDecodeDifferently) {
    const std::array<std::uint8_t, 8> data{0x12, 0x34};
    const auto intel = extract_bits(data.data(), data.size(), 0, 16, ByteOrder::Intel);
    const auto motorola = extract_bits(data.data(), data.size(), 7, 16, ByteOrder::Motorola);

    ASSERT_TRUE(intel.has_value());
    ASSERT_TRUE(motorola.has_value());
    EXPECT_EQ(intel.value(), 0x3412U);
    EXPECT_EQ(motorola.value(), 0x1234U);
    EXPECT_NE(intel.value(), motorola.value());
}

// --- Round trip ------------------------------------------------------------

/// Writes `value` at one placement and reads it back. A placement that does
/// not fit is skipped, not failed -- the point is that everything which *can*
/// be written survives being read.
void expect_round_trip(unsigned start, unsigned length, ByteOrder order, std::uint64_t value) {
    std::array<std::uint8_t, 8> data{};
    if (!insert_bits(data.data(), data.size(), start, length, order, value)) {
        return;
    }
    const auto got = extract_bits(data.data(), data.size(), start, length, order);
    ASSERT_TRUE(got.has_value()) << order_name(order) << " start=" << start << " len=" << length;
    EXPECT_EQ(got.value(), value) << order_name(order) << " start=" << start << " len=" << length;
}

TEST(RoundTrip, EveryPlacementThatFitsSurvives) {
    constexpr std::uint64_t kPattern = 0x0123'4567'89AB'CDEFULL;

    for (unsigned length = 1U; length <= 32U; ++length) {
        const std::uint64_t value = kPattern & ((std::uint64_t{1} << length) - 1U);
        for (unsigned start = 0U; start < 64U; ++start) {
            for (const ByteOrder order : {ByteOrder::Intel, ByteOrder::Motorola}) {
                expect_round_trip(start, length, order, value);
            }
        }
    }
}

TEST(RoundTrip, NeighbouringBitsAreLeftAlone) {
    for (const ByteOrder order : {ByteOrder::Intel, ByteOrder::Motorola}) {
        std::array<std::uint8_t, 8> data{};
        data.fill(0xFF);
        // Clear an 8-bit hole covering byte 2 and check nothing else moved.
        ASSERT_TRUE(insert_bits(data.data(), data.size(),
                                order == ByteOrder::Intel ? 16U : 23U, 8U, order, 0x00U))
            << order_name(order);
        const std::array<std::uint8_t, 8> expected{0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        EXPECT_EQ(data, expected) << order_name(order);
    }
}

TEST(RoundTrip, InsertRejectsAValueThatDoesNotFitTheFrame) {
    std::array<std::uint8_t, 8> data{};
    data.fill(0xFF);
    const std::array<std::uint8_t, 8> before = data;

    EXPECT_FALSE(insert_bits(data.data(), data.size(), 60, 16, ByteOrder::Intel, 0xFFFFU));
    EXPECT_EQ(data, before) << "a rejected insert must not touch the frame";
}

// --- Rejected geometry -----------------------------------------------------

TEST(Geometry, SignalRunningOffTheEndIsRejected) {
    const std::array<std::uint8_t, 8> data{};
    EXPECT_FALSE(extract_bits(data.data(), data.size(), 60, 16, ByteOrder::Intel).has_value());
    EXPECT_FALSE(extract_bits(data.data(), data.size(), 63, 16, ByteOrder::Motorola).has_value());
}

TEST(Geometry, ShorterFrameShrinksWhatFits) {
    const std::array<std::uint8_t, 8> data{0x34, 0x12};
    // The same signal is fine in an 8-byte frame and impossible in a 1-byte one.
    EXPECT_TRUE(extract_bits(data.data(), 8, 0, 16, ByteOrder::Intel).has_value());
    EXPECT_FALSE(extract_bits(data.data(), 1, 0, 16, ByteOrder::Intel).has_value());
}

TEST(Geometry, DegenerateArgumentsAreRejected) {
    const std::array<std::uint8_t, 8> data{};
    EXPECT_FALSE(extract_bits(data.data(), data.size(), 0, 0, ByteOrder::Intel).has_value());
    EXPECT_FALSE(extract_bits(data.data(), data.size(), 0, 65, ByteOrder::Intel).has_value());
    EXPECT_FALSE(extract_bits(nullptr, 8, 0, 8, ByteOrder::Intel).has_value());
    EXPECT_FALSE(insert_bits(nullptr, 8, 0, 8, ByteOrder::Intel, 0));
}

// --- Sign extension --------------------------------------------------------

TEST(SignExtend, PositiveValuesAreUnchanged) {
    EXPECT_EQ(sign_extend(0x7FFFU, 16), 32767);
    EXPECT_EQ(sign_extend(0x0001U, 16), 1);
}

TEST(SignExtend, NegativeValuesGetTheirHighBitsFilled) {
    EXPECT_EQ(sign_extend(0xFFFFU, 16), -1);
    EXPECT_EQ(sign_extend(0x8000U, 16), -32768);
    EXPECT_EQ(sign_extend(0xFFU, 8), -1);
    EXPECT_EQ(sign_extend(0x1FU, 5), -1);  // 5-bit signal, all ones
}

TEST(SignExtend, ForgettingTheSignIsTheClassicBug) {
    const std::array<std::uint8_t, 8> data{0xFF, 0xFF};
    const auto raw = extract_bits(data.data(), data.size(), 0, 16, ByteOrder::Intel);
    ASSERT_TRUE(raw.has_value());

    EXPECT_EQ(raw.value(), 65535U);               // read as unsigned
    EXPECT_EQ(sign_extend(raw.value(), 16), -1);  // read as the DBC actually meant
}

TEST(SignExtend, SixtyFourBitSignalsAreAPassThrough) {
    EXPECT_EQ(sign_extend(~std::uint64_t{0}, 64), -1);
}

}  // namespace
