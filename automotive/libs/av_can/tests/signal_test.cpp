#include "av/can/signal.hpp"

#include <gtest/gtest.h>

#include <limits>

#include "av/can/bit.hpp"
#include "av/can/frame.hpp"

namespace {

using av::can::ByteOrder;
using av::can::CanFrame;
using av::can::decode;
using av::can::encode;
using av::can::SignalSpec;

// The message this whole week is built around. In DBC it would read:
//
//   BO_ 256 EngineData: 8 ECU1
//    SG_ VehicleSpeed  :  0|16@1+ (0.01,0)  "km/h"
//    SG_ EngineRpm     : 23|16@0+ (0.25,0)  "rpm"
//    SG_ CoolantTemp   : 32| 8@1+ (1,-40)   "degC"
//    SG_ SteeringAngle : 40|16@1- (0.1,0)   "deg"

constexpr SignalSpec kVehicleSpeed{"VehicleSpeed", 0U, 16U, ByteOrder::Intel,
                                   false, 0.01, 0.0, "km/h"};
constexpr SignalSpec kEngineRpm{"EngineRpm", 23U, 16U, ByteOrder::Motorola,
                                false, 0.25, 0.0, "rpm"};
constexpr SignalSpec kCoolantTemp{"CoolantTemp", 32U, 8U, ByteOrder::Intel,
                                  false, 1.0, -40.0, "degC"};
constexpr SignalSpec kSteeringAngle{"SteeringAngle", 40U, 16U, ByteOrder::Intel,
                                    true, 0.1, 0.0, "deg"};

constexpr double kEpsilon = 1e-9;

CanFrame make_frame() {
    CanFrame frame{};
    frame.id = 0x100U;
    frame.length = 8U;
    return frame;
}

// --- Decoding known bytes --------------------------------------------------

TEST(Decode, VehicleSpeedAppliesTheFactor) {
    CanFrame frame = make_frame();
    frame.data[0] = 0x3AU;  // raw = 0x203A = 8250, little-endian
    frame.data[1] = 0x20U;

    const auto value = decode(kVehicleSpeed, frame);
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(value.value(), 82.5, kEpsilon);
}

TEST(Decode, EngineRpmReadsTheMotorolaBytes) {
    CanFrame frame = make_frame();
    frame.data[2] = 0x0CU;  // raw = 0x0C80 = 3200, big-endian
    frame.data[3] = 0x80U;

    const auto value = decode(kEngineRpm, frame);
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(value.value(), 800.0, kEpsilon);
}

TEST(Decode, CoolantTempAppliesTheOffset) {
    CanFrame frame = make_frame();
    frame.data[4] = 130U;  // 130 - 40 = 90

    const auto value = decode(kCoolantTemp, frame);
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(value.value(), 90.0, kEpsilon);

    frame.data[4] = 0U;  // the offset is what lets the signal go below zero
    EXPECT_NEAR(decode(kCoolantTemp, frame).value(), -40.0, kEpsilon);
}

TEST(Decode, SteeringAngleIsSigned) {
    CanFrame frame = make_frame();
    frame.data[5] = 0x83U;  // raw = 0xFF83 = -125 as int16
    frame.data[6] = 0xFFU;

    const auto value = decode(kSteeringAngle, frame);
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(value.value(), -12.5, kEpsilon);
}

TEST(Decode, TheSameSignalReadUnsignedGivesNonsense) {
    // Dropping the `-` from `@1-` in a DBC row is a one-character mistake that
    // turns a small left turn into a hard right one.
    SignalSpec wrong = kSteeringAngle;
    wrong.is_signed = false;

    CanFrame frame = make_frame();
    frame.data[5] = 0x83U;
    frame.data[6] = 0xFFU;

    EXPECT_NEAR(decode(kSteeringAngle, frame).value(), -12.5, kEpsilon);
    EXPECT_NEAR(decode(wrong, frame).value(), 6541.1, 1e-6);
}

TEST(Decode, TheSameSignalReadWithTheWrongByteOrderGivesNonsense) {
    CanFrame frame = make_frame();
    frame.data[0] = 0x3AU;
    frame.data[1] = 0x20U;

    SignalSpec swapped = kVehicleSpeed;
    swapped.order = ByteOrder::Motorola;
    swapped.start_bit = 7U;  // Motorola counts from the MSB of byte 0

    EXPECT_NEAR(decode(kVehicleSpeed, frame).value(), 82.5, kEpsilon);
    EXPECT_NEAR(decode(swapped, frame).value(), 148.8, 1e-6);  // 0x3A20 = 14880
}

// --- Round trip ------------------------------------------------------------

TEST(Encode, RoundTripsThroughTheFrame) {
    CanFrame frame = make_frame();

    ASSERT_TRUE(encode(kVehicleSpeed, 82.5, frame));
    ASSERT_TRUE(encode(kEngineRpm, 800.0, frame));
    ASSERT_TRUE(encode(kCoolantTemp, 90.0, frame));
    ASSERT_TRUE(encode(kSteeringAngle, -12.5, frame));

    EXPECT_NEAR(decode(kVehicleSpeed, frame).value(), 82.5, kEpsilon);
    EXPECT_NEAR(decode(kEngineRpm, frame).value(), 800.0, kEpsilon);
    EXPECT_NEAR(decode(kCoolantTemp, frame).value(), 90.0, kEpsilon);
    EXPECT_NEAR(decode(kSteeringAngle, frame).value(), -12.5, kEpsilon);
}

TEST(Encode, PacksSignalsWithoutDisturbingEachOther) {
    CanFrame frame = make_frame();
    ASSERT_TRUE(encode(kVehicleSpeed, 82.5, frame));
    ASSERT_TRUE(encode(kEngineRpm, 800.0, frame));

    // Rewriting one signal must leave the other where it was.
    ASSERT_TRUE(encode(kVehicleSpeed, 0.0, frame));
    EXPECT_NEAR(decode(kEngineRpm, frame).value(), 800.0, kEpsilon);
    EXPECT_NEAR(decode(kVehicleSpeed, frame).value(), 0.0, kEpsilon);
}

TEST(Encode, ProducesTheBytesTheDbcPromises) {
    CanFrame frame = make_frame();
    ASSERT_TRUE(encode(kVehicleSpeed, 82.5, frame));
    EXPECT_EQ(frame.data[0], 0x3AU);
    EXPECT_EQ(frame.data[1], 0x20U);
}

TEST(Encode, RoundsToTheNearestRepresentableStep) {
    CanFrame frame = make_frame();
    // 0.25 rpm resolution: 800.1 is not on the grid.
    ASSERT_TRUE(encode(kEngineRpm, 800.1, frame));
    EXPECT_NEAR(decode(kEngineRpm, frame).value(), 800.0, kEpsilon);
}

// --- Refusals --------------------------------------------------------------

TEST(Encode, RejectsAValueTheSignalCannotHold) {
    CanFrame frame = make_frame();
    const auto before = frame.data;

    // 16 bits at 0.01 km/h tops out at 655.35.
    EXPECT_FALSE(encode(kVehicleSpeed, 700.0, frame));
    EXPECT_FALSE(encode(kVehicleSpeed, -1.0, frame));
    EXPECT_EQ(frame.data, before) << "a rejected encode must not touch the frame";
}

TEST(Encode, RejectsSignedValuesOutsideTwosComplementRange) {
    CanFrame frame = make_frame();
    EXPECT_TRUE(encode(kSteeringAngle, 3276.7, frame));
    EXPECT_FALSE(encode(kSteeringAngle, 3276.8, frame));
    EXPECT_TRUE(encode(kSteeringAngle, -3276.8, frame));
    EXPECT_FALSE(encode(kSteeringAngle, -3276.9, frame));
}

TEST(Encode, RejectsNonFiniteInput) {
    CanFrame frame = make_frame();
    EXPECT_FALSE(encode(kVehicleSpeed, std::numeric_limits<double>::quiet_NaN(), frame));
    EXPECT_FALSE(encode(kVehicleSpeed, std::numeric_limits<double>::infinity(), frame));
}

TEST(Encode, RejectsADegenerateSpec) {
    CanFrame frame = make_frame();
    SignalSpec broken = kVehicleSpeed;

    broken.factor = 0.0;
    EXPECT_FALSE(encode(broken, 10.0, frame));

    broken = kVehicleSpeed;
    broken.length = 0U;
    EXPECT_FALSE(encode(broken, 10.0, frame));
}

TEST(Decode, TruncatedFrameYieldsNothing) {
    CanFrame frame = make_frame();
    frame.length = 2U;  // only VehicleSpeed made it

    EXPECT_TRUE(decode(kVehicleSpeed, frame).has_value());
    EXPECT_FALSE(decode(kEngineRpm, frame).has_value())
        << "a signal past the end of a short frame is not a value of zero";
    EXPECT_FALSE(decode(kCoolantTemp, frame).has_value());
}

TEST(Decode, EmptyFrameYieldsNothing) {
    CanFrame frame = make_frame();
    frame.length = 0U;
    EXPECT_FALSE(decode(kVehicleSpeed, frame).has_value());
}

}  // namespace
