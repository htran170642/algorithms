#include "av/can/arbitration.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using av::can::arbitrate;
using av::can::arbitration_bit_name;
using av::can::arbitration_field;
using av::can::BitValue;
using av::can::Contender;
using av::can::kExtendedArbitrationBits;
using av::can::kStandardArbitrationBits;

/// 0x123 shifted into ID28..ID18: an extended frame whose *base* identifier is
/// the same 11 bits as a standard 0x123 frame. Everything interesting about
/// standard-versus-extended arbitration lives in this collision.
constexpr std::uint32_t kExtendedWithBase123 = 0x123U << 18U;

// --- The field itself ------------------------------------------------------

TEST(ArbitrationField, HasThirteenBitsForAStandardFrame) {
    const auto bits = arbitration_field(Contender{"ECU", 0x123U, false, false});
    EXPECT_EQ(bits.size(), kStandardArbitrationBits);
}

TEST(ArbitrationField, HasThirtyTwoBitsForAnExtendedFrame) {
    const auto bits = arbitration_field(Contender{"ECU", kExtendedWithBase123, true, false});
    EXPECT_EQ(bits.size(), kExtendedArbitrationBits);
}

TEST(ArbitrationField, SendsTheIdentifierMostSignificantBitFirst) {
    // 0x555 = 101 0101 0101. A 1 is recessive, a 0 is dominant.
    const auto bits = arbitration_field(Contender{"ECU", 0x555U, false, false});
    ASSERT_EQ(bits.size(), kStandardArbitrationBits);

    const std::vector<BitValue> expected_id{
        BitValue::Recessive, BitValue::Dominant, BitValue::Recessive, BitValue::Dominant,
        BitValue::Recessive, BitValue::Dominant, BitValue::Recessive, BitValue::Dominant,
        BitValue::Recessive, BitValue::Dominant, BitValue::Recessive,
    };
    EXPECT_TRUE(std::equal(expected_id.begin(), expected_id.end(), bits.begin()));

    EXPECT_EQ(bits[11], BitValue::Dominant) << "RTR: a data frame drives it dominant";
    EXPECT_EQ(bits[12], BitValue::Dominant) << "IDE: dominant means an 11-bit identifier";
}

TEST(ArbitrationField, ARemoteFrameDrivesRtrRecessive) {
    const auto data = arbitration_field(Contender{"ECU", 0x123U, false, false});
    const auto remote = arbitration_field(Contender{"ECU", 0x123U, false, true});
    EXPECT_EQ(data[11], BitValue::Dominant);
    EXPECT_EQ(remote[11], BitValue::Recessive);
}

TEST(ArbitrationField, AnExtendedFrameDrivesSrrAndIdeRecessive) {
    const auto bits = arbitration_field(Contender{"ECU", kExtendedWithBase123, true, false});
    EXPECT_EQ(bits[11], BitValue::Recessive) << "SRR";
    EXPECT_EQ(bits[12], BitValue::Recessive) << "IDE: 18 more identifier bits follow";
}

TEST(ArbitrationBitName, FollowsTheTwentyNineBitNumbering) {
    EXPECT_EQ(arbitration_bit_name(0), "ID28");
    EXPECT_EQ(arbitration_bit_name(10), "ID18");
    EXPECT_EQ(arbitration_bit_name(11), "RTR/SRR");
    EXPECT_EQ(arbitration_bit_name(12), "IDE");
    EXPECT_EQ(arbitration_bit_name(13), "ID17");
    EXPECT_EQ(arbitration_bit_name(30), "ID0");
    EXPECT_EQ(arbitration_bit_name(31), "RTR");
    EXPECT_TRUE(arbitration_bit_name(32).empty());
}

// --- Priority --------------------------------------------------------------

TEST(Arbitrate, TheLowestIdentifierWins) {
    const std::vector<Contender> bus{
        {"powertrain", 0x100U, false, false},
        {"body", 0x200U, false, false},
        {"brakes", 0x080U, false, false},
    };
    const auto result = arbitrate(bus);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->winners.size(), 1U);
    EXPECT_EQ(result->winners.front(), 2U) << "0x080 is numerically lowest";
}

TEST(Arbitrate, LosersWithdrawAtTheFirstBitWhereTheyDiffer) {
    // 0x100 = 001 0000 0000
    // 0x200 = 010 0000 0000
    // 0x080 = 000 1000 0000
    // Bit 0 (ID28) is dominant for all three. Bit 1 separates 0x200; bit 2
    // separates 0x100. Nobody transmits a single wrong bit -- that is what
    // makes arbitration non-destructive.
    const std::vector<Contender> bus{
        {"powertrain", 0x100U, false, false},
        {"body", 0x200U, false, false},
        {"brakes", 0x080U, false, false},
    };
    const auto result = arbitrate(bus);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->trace.size(), 3U);

    EXPECT_TRUE(result->trace[0].lost.empty());
    EXPECT_EQ(result->trace[0].bus, BitValue::Dominant);

    EXPECT_EQ(result->trace[1].lost, (std::vector<std::size_t>{1U})) << "0x200 drops out";
    EXPECT_EQ(result->trace[2].lost, (std::vector<std::size_t>{0U})) << "0x100 drops out";

    // The winner never appears in any loss list.
    for (const auto& step : result->trace) {
        for (const std::size_t node : step.lost) {
            EXPECT_NE(node, 2U);
        }
    }
}

TEST(Arbitrate, ADataFrameBeatsARemoteFrameWithTheSameIdentifier) {
    const std::vector<Contender> bus{
        {"asks", 0x123U, false, true},
        {"answers", 0x123U, false, false},
    };
    const auto result = arbitrate(bus);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->winners.size(), 1U);
    EXPECT_EQ(result->winners.front(), 1U);

    ASSERT_EQ(result->trace.size(), 12U);
    EXPECT_EQ(result->trace.back().index, 11U) << "decided on RTR";
    EXPECT_EQ(result->trace.back().name, "RTR/SRR");
}

TEST(Arbitrate, AStandardFrameBeatsAnExtendedFrameWithTheSameBaseIdentifier) {
    const std::vector<Contender> bus{
        {"extended", kExtendedWithBase123, true, false},
        {"standard", 0x123U, false, false},
    };
    const auto result = arbitrate(bus);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->winners.size(), 1U);
    EXPECT_EQ(result->winners.front(), 1U);

    // Position 11 is RTR for the standard frame (dominant) and SRR for the
    // extended one (always recessive). The extended frame cannot win there.
    ASSERT_EQ(result->trace.size(), 12U);
    EXPECT_EQ(result->trace.back().index, 11U);
    EXPECT_EQ(result->trace.back().lost, (std::vector<std::size_t>{0U}));
}

TEST(Arbitrate, AStandardRemoteFrameStillBeatsAnExtendedFrame) {
    // Both drive position 11 recessive, so the contest survives one bit longer
    // and IDE decides it -- with the same outcome.
    const std::vector<Contender> bus{
        {"extended", kExtendedWithBase123, true, false},
        {"standard remote", 0x123U, false, true},
    };
    const auto result = arbitrate(bus);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->winners.size(), 1U);
    EXPECT_EQ(result->winners.front(), 1U);

    ASSERT_EQ(result->trace.size(), 13U);
    EXPECT_TRUE(result->trace[11].lost.empty()) << "RTR and SRR are both recessive here";
    EXPECT_EQ(result->trace[12].name, "IDE");
    EXPECT_EQ(result->trace[12].lost, (std::vector<std::size_t>{0U}));
}

TEST(Arbitrate, ExtendedFramesAreOrderedByTheFullTwentyNineBits) {
    const std::vector<Contender> bus{
        {"high", (0x123U << 18U) | 0x00010U, true, false},
        {"low", (0x123U << 18U) | 0x00001U, true, false},
    };
    const auto result = arbitrate(bus);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->winners.size(), 1U);
    EXPECT_EQ(result->winners.front(), 1U);
    EXPECT_GT(result->trace.size(), 13U) << "the extension bits decided it, not the base id";
}

// --- Faults ----------------------------------------------------------------

TEST(Arbitrate, IdenticalIdentifiersCannotBeResolved) {
    // Two ECUs configured with the same id both survive arbitration and then
    // corrupt each other in the data field. On a real bus this shows up as
    // sporadic error frames, never as a priority complaint.
    const std::vector<Contender> bus{
        {"ecu-a", 0x123U, false, false},
        {"ecu-b", 0x123U, false, false},
    };
    const auto result = arbitrate(bus);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->winners.size(), 2U);
    EXPECT_EQ(result->trace.size(), kStandardArbitrationBits);
}

TEST(Arbitrate, RejectsAnIdentifierOutsideItsFormatsMask) {
    EXPECT_FALSE(arbitrate({{"too big", 0x800U, false, false}}).has_value());
    EXPECT_FALSE(arbitrate({{"too big", 0x2000'0000U, true, false}}).has_value());
}

TEST(Arbitrate, RejectsAnEmptyBus) {
    EXPECT_FALSE(arbitrate({}).has_value());
}

TEST(Arbitrate, ASingleTransmitterNeedsNoContest) {
    const auto result = arbitrate({{"alone", 0x123U, false, false}});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->winners, (std::vector<std::size_t>{0U}));
    EXPECT_TRUE(result->trace.empty());
}

}  // namespace
