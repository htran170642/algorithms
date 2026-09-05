#include "av/can/dbc.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <optional>
#include <sstream>
#include <string>

#include "av/can/bit.hpp"
#include "av/can/frame.hpp"

// CMake always defines this (libs/av_can/CMakeLists.txt). The fallback exists
// only so the file still parses in an editor that has not read
// compile_commands.json yet; the relative path would not find the file at run
// time, and the test says so in its failure message.
#ifndef AV_DBC_PATH
#define AV_DBC_PATH "dbc/cockpit.dbc"
#endif

namespace {

using av::can::ByteOrder;
using av::can::CanFrame;
using av::can::DbcDatabase;

/// Parsing from a string keeps most of these tests independent of any file on
/// disk. Only the last group opens the real dbc/cockpit.dbc.
std::optional<DbcDatabase> parse(const std::string& text) {
    std::istringstream in(text);
    return DbcDatabase::parse(in);
}

constexpr const char* kMinimal = R"(VERSION "unit-test 1.0"

BO_ 256 EngineData: 8 ECU_Powertrain
 SG_ VehicleSpeed : 0|16@1+ (0.01,0) [0|655.35] "km/h" Cluster
)";

// --- Structure -------------------------------------------------------------

TEST(Dbc, ParsesTheVersionString) {
    const auto db = parse(kMinimal);
    ASSERT_TRUE(db.has_value());
    EXPECT_EQ(db->version(), "unit-test 1.0");
}

TEST(Dbc, ParsesAMessageHeader) {
    const auto db = parse(kMinimal);
    ASSERT_TRUE(db.has_value());
    ASSERT_EQ(db->messages().size(), 1U);

    const auto& message = db->messages().front();
    EXPECT_EQ(message.id, 256U);
    EXPECT_FALSE(message.extended);
    EXPECT_EQ(message.name, "EngineData");
    EXPECT_EQ(message.length, 8U);
    EXPECT_EQ(message.transmitter, "ECU_Powertrain");
}

TEST(Dbc, ParsesEverySignalField) {
    const auto db = parse(kMinimal);
    ASSERT_TRUE(db.has_value());
    const auto* signal = db->messages().front().find("VehicleSpeed");
    ASSERT_NE(signal, nullptr);

    EXPECT_EQ(signal->start_bit, 0U);
    EXPECT_EQ(signal->length, 16U);
    EXPECT_EQ(signal->order, ByteOrder::Intel);
    EXPECT_FALSE(signal->is_signed);
    EXPECT_DOUBLE_EQ(signal->factor, 0.01);
    EXPECT_DOUBLE_EQ(signal->offset, 0.0);
    EXPECT_DOUBLE_EQ(signal->minimum, 0.0);
    EXPECT_DOUBLE_EQ(signal->maximum, 655.35);
    EXPECT_TRUE(signal->has_range);
    EXPECT_EQ(signal->unit, "km/h");
}

TEST(Dbc, ParsesMotorolaOrderAndSignedValues) {
    const auto db = parse(R"(BO_ 100 M: 8 ECU
 SG_ Big : 7|16@0- (0.1,-5) [-100|100] "deg" Cluster
)");
    ASSERT_TRUE(db.has_value());
    const auto* signal = db->messages().front().find("Big");
    ASSERT_NE(signal, nullptr);
    EXPECT_EQ(signal->order, ByteOrder::Motorola) << "@0 is Motorola";
    EXPECT_TRUE(signal->is_signed) << "- means two's complement";
    EXPECT_DOUBLE_EQ(signal->offset, -5.0);
}

TEST(Dbc, TreatsAZeroZeroRangeAsNoRange) {
    // Many tools emit [0|0] for "no range stated". Enforcing that literally
    // would reject every value except zero.
    const auto db = parse(R"(BO_ 100 M: 8 ECU
 SG_ Free : 0|8@1+ (1,0) [0|0] "" Cluster
)");
    ASSERT_TRUE(db.has_value());
    const auto* signal = db->messages().front().find("Free");
    ASSERT_NE(signal, nullptr);
    EXPECT_FALSE(signal->has_range);
    EXPECT_TRUE(signal->in_range(200.0));
}

// --- The extended-id trap --------------------------------------------------

TEST(Dbc, ReadsBitThirtyOneAsTheExtendedFlag) {
    // 2147483939 == 0x80000123. The id is 0x123 and the frame is 29-bit.
    // A parser that takes the number literally builds a message that nothing
    // on the bus will ever match.
    const auto db = parse(R"(BO_ 2147483939 Ext: 8 ECU
 SG_ S : 0|8@1+ (1,0) [0|255] "" Cluster
)");
    ASSERT_TRUE(db.has_value());
    const auto& message = db->messages().front();
    EXPECT_TRUE(message.extended);
    EXPECT_EQ(message.id, 0x123U) << "the flag bit must not survive into the id";
}

TEST(Dbc, StandardAndExtendedAreDifferentMessages) {
    const auto db = parse(R"(BO_ 291 Std: 8 ECU
 SG_ A : 0|8@1+ (1,0) [0|255] "" Cluster
BO_ 2147483939 Ext: 8 ECU
 SG_ B : 0|8@1+ (1,0) [0|255] "" Cluster
)");
    ASSERT_TRUE(db.has_value());
    ASSERT_EQ(db->messages().size(), 2U);

    const auto* standard = db->find(0x123U, false);
    const auto* extended = db->find(0x123U, true);
    ASSERT_NE(standard, nullptr);
    ASSERT_NE(extended, nullptr);
    EXPECT_EQ(standard->name, "Std");
    EXPECT_EQ(extended->name, "Ext");
}

TEST(Dbc, FindReportsAnAbsentMessage) {
    const auto db = parse(kMinimal);
    ASSERT_TRUE(db.has_value());
    EXPECT_EQ(db->find(0x999U), nullptr);
}

// --- Comments and value tables ---------------------------------------------

TEST(Dbc, AttachesCommentsToMessagesAndSignals) {
    const auto db = parse(R"(BO_ 256 EngineData: 8 ECU
 SG_ VehicleSpeed : 0|16@1+ (0.01,0) [0|655.35] "km/h" Cluster
CM_ BO_ 256 "cyclic, 100 ms";
CM_ SG_ 256 VehicleSpeed "filtered road speed";
)");
    ASSERT_TRUE(db.has_value());
    const auto& message = db->messages().front();
    EXPECT_EQ(message.comment, "cyclic, 100 ms");
    ASSERT_NE(message.find("VehicleSpeed"), nullptr);
    EXPECT_EQ(message.find("VehicleSpeed")->comment, "filtered road speed");
}

TEST(Dbc, AttachesValueTablesAndNamesDecodedValues) {
    const auto db = parse(R"(BO_ 512 BodyState: 8 ECU
 SG_ DoorStatus : 0|4@1+ (1,0) [0|15] "" Cluster
VAL_ 512 DoorStatus 0 "AllClosed" 1 "DriverOpen" 2 "PassengerOpen" ;
)");
    ASSERT_TRUE(db.has_value());
    const auto* signal = db->messages().front().find("DoorStatus");
    ASSERT_NE(signal, nullptr);
    ASSERT_EQ(signal->values.size(), 3U);

    EXPECT_EQ(signal->value_name(0.0), "AllClosed");
    EXPECT_EQ(signal->value_name(1.0), "DriverOpen");
    EXPECT_TRUE(signal->value_name(9.0).empty()) << "no entry: no invented name";
}

// --- Range enforcement, the promise week 1 made ----------------------------

TEST(Dbc, EncodeRefusesAValueOutsideTheStatedRange) {
    // signal.hpp said the range "arrives in week 6 together with the DBC
    // parser". This is that. A transmitter that cannot honour the DBC must
    // not put the frame on the bus at all.
    const auto db = parse(R"(BO_ 100 M: 8 ECU
 SG_ Temp : 0|8@1+ (1,-40) [-40|100] "degC" Cluster
)");
    ASSERT_TRUE(db.has_value());
    const auto* signal = db->messages().front().find("Temp");
    ASSERT_NE(signal, nullptr);

    CanFrame frame{};
    frame.length = 8;

    EXPECT_TRUE(signal->encode(90.0, frame)) << "inside the range";
    EXPECT_FALSE(signal->encode(150.0, frame)) << "above max, even though 190 fits 8 bits";
    EXPECT_FALSE(signal->encode(-50.0, frame)) << "below min";
    EXPECT_TRUE(signal->in_range(-40.0)) << "the bounds are inclusive";
    EXPECT_TRUE(signal->in_range(100.0));
}

TEST(Dbc, DecodeStillReportsAnOutOfRangeValue) {
    // Deliberate asymmetry. An out-of-range value on the bus is a *fact*: the
    // receiver must be able to see it, log it and decide. Dropping it here
    // would hide the very fault the range exists to reveal.
    const auto db = parse(R"(BO_ 100 M: 8 ECU
 SG_ Temp : 0|8@1+ (1,-40) [-40|100] "degC" Cluster
)");
    ASSERT_TRUE(db.has_value());
    const auto* signal = db->messages().front().find("Temp");
    ASSERT_NE(signal, nullptr);

    CanFrame frame{};
    frame.length = 8;
    frame.data[0] = 250;  // 250 - 40 = 210 degC, far above max

    const auto value = signal->decode(frame);
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 210.0);
    EXPECT_FALSE(signal->in_range(*value)) << "the caller decides what to do";
}

// --- Malformed input -------------------------------------------------------

TEST(Dbc, RejectsASignalBeforeAnyMessage) {
    EXPECT_FALSE(parse(" SG_ Orphan : 0|8@1+ (1,0) [0|255] \"\" X\n").has_value());
}

TEST(Dbc, RejectsAMalformedSignalLine) {
    EXPECT_FALSE(parse(R"(BO_ 100 M: 8 ECU
 SG_ Broken : 0|8@9+ (1,0) [0|255] "" Cluster
)")
                     .has_value())
        << "@9 is neither Intel nor Motorola";

    EXPECT_FALSE(parse(R"(BO_ 100 M: 8 ECU
 SG_ Broken : 0-8@1+ (1,0) [0|255] "" Cluster
)")
                     .has_value())
        << "'-' where '|' belongs";
}

TEST(Dbc, RejectsMultiplexedSignals) {
    // Not supported, and it says so out loud. Loading half a database would be
    // worse: the missing signals would look like a quiet bus.
    EXPECT_FALSE(parse(R"(BO_ 100 M: 8 ECU
 SG_ Muxed m0 : 0|8@1+ (1,0) [0|255] "" Cluster
)")
                     .has_value());
}

TEST(Dbc, RejectsAnElevenBitIdThatDoesNotFit) {
    EXPECT_FALSE(parse("BO_ 4096 TooBig: 8 ECU\n SG_ S : 0|8@1+ (1,0) [0|1] \"\" X\n").has_value());
}

TEST(Dbc, RejectsAPayloadLongerThanCanFd) {
    EXPECT_FALSE(parse("BO_ 100 M: 65 ECU\n SG_ S : 0|8@1+ (1,0) [0|1] \"\" X\n").has_value());
}

TEST(Dbc, RejectsAFileWithNoMessages) {
    EXPECT_FALSE(parse("VERSION \"empty\"\n\nBS_:\n\nBU_ A B C\n").has_value());
}

TEST(Dbc, ReportsAMissingFile) {
    EXPECT_FALSE(DbcDatabase::load("/nonexistent/av-no-such.dbc").has_value());
}

// --- The real file ---------------------------------------------------------

TEST(Dbc, LoadsTheProjectDatabase) {
    const auto db = DbcDatabase::load(AV_DBC_PATH);
    ASSERT_TRUE(db.has_value()) << "path: " << AV_DBC_PATH;
    EXPECT_EQ(db->messages().size(), 2U);

    const auto* engine = db->find(0x100U);
    const auto* body = db->find(0x200U);
    ASSERT_NE(engine, nullptr);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(engine->signals.size(), 4U);
    EXPECT_EQ(body->signals.size(), 3U);

    // CLAUDE.md section 7 requires all seven.
    for (const char* name : {"VehicleSpeed", "EngineRpm", "CoolantTemp", "FuelLevel"}) {
        EXPECT_NE(engine->find(name), nullptr) << name;
    }
    for (const char* name : {"SteeringAngle", "DoorStatus", "WarningStatus"}) {
        EXPECT_NE(body->find(name), nullptr) << name;
    }
}

TEST(Dbc, RoundTripsEverySignalOfTheProjectDatabase) {
    const auto db = DbcDatabase::load(AV_DBC_PATH);
    ASSERT_TRUE(db.has_value());

    const auto* engine = db->find(0x100U);
    ASSERT_NE(engine, nullptr);

    CanFrame frame{};
    frame.id = 0x100U;
    frame.length = engine->length;

    const std::array<double, 4> sent{73.72, 2448.5, 88.0, 59.5};
    for (std::size_t i = 0; i < engine->signals.size(); ++i) {
        ASSERT_TRUE(engine->signals[i].encode(sent[i], frame)) << engine->signals[i].name;
    }
    for (std::size_t i = 0; i < engine->signals.size(); ++i) {
        const auto back = engine->signals[i].decode(frame);
        ASSERT_TRUE(back.has_value()) << engine->signals[i].name;
        // Each value sits exactly on its quantisation step, so this round trip
        // is lossless. Off-step values are not; that is quantisation, not
        // corruption.
        EXPECT_DOUBLE_EQ(*back, sent[i]) << engine->signals[i].name;
    }
}

TEST(Dbc, TheSignedSignalSurvivesANegativeValue) {
    const auto db = DbcDatabase::load(AV_DBC_PATH);
    ASSERT_TRUE(db.has_value());
    const auto* body = db->find(0x200U);
    ASSERT_NE(body, nullptr);
    const auto* steering = body->find("SteeringAngle");
    ASSERT_NE(steering, nullptr);
    ASSERT_TRUE(steering->is_signed);

    CanFrame frame{};
    frame.id = 0x200U;
    frame.length = body->length;
    ASSERT_TRUE(steering->encode(-12.5, frame));

    const auto back = steering->decode(frame);
    ASSERT_TRUE(back.has_value());
    EXPECT_DOUBLE_EQ(*back, -12.5) << "sign extension, not 6553.5";
}

TEST(Dbc, NamesTheDoorAndWarningBitfields) {
    const auto db = DbcDatabase::load(AV_DBC_PATH);
    ASSERT_TRUE(db.has_value());
    const auto* body = db->find(0x200U);
    ASSERT_NE(body, nullptr);

    ASSERT_NE(body->find("DoorStatus"), nullptr);
    ASSERT_NE(body->find("WarningStatus"), nullptr);
    EXPECT_EQ(body->find("DoorStatus")->value_name(1.0), "DriverOpen");
    EXPECT_EQ(body->find("WarningStatus")->value_name(4.0), "Overheat");
}

}  // namespace
