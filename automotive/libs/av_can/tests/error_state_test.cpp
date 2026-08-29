#include "av/can/error_state.hpp"

#include <gtest/gtest.h>

namespace {

using av::can::ErrorCounters;
using av::can::ErrorEvent;
using av::can::ErrorState;
using av::can::kBusOffLimit;
using av::can::kErrorPassiveLimit;
using av::can::kRecoveryWindows;
using av::can::to_string;

/// Applies the same event `times` times, because most of these rules are only
/// visible as an accumulation.
void repeat(ErrorCounters& node, ErrorEvent event, unsigned times) {
    for (unsigned i = 0; i < times; ++i) {
        node.on(event);
    }
}

// --- Starting point --------------------------------------------------------

TEST(ErrorCounters, PowersOnErrorActive) {
    const ErrorCounters node;
    EXPECT_EQ(node.state(), ErrorState::Active);
    EXPECT_EQ(node.transmit_errors(), 0U);
    EXPECT_EQ(node.receive_errors(), 0U);
}

// --- The 8:1 asymmetry -----------------------------------------------------

TEST(ErrorCounters, OneTransmitErrorCostsEight) {
    ErrorCounters node;
    node.on(ErrorEvent::TransmitError);
    EXPECT_EQ(node.transmit_errors(), 8U);
}

TEST(ErrorCounters, OneReceiveErrorCostsOnlyOne) {
    // The node that was transmitting is the likeliest culprit, so it pays eight
    // times what a listener pays. This is what makes a faulty ECU remove itself
    // instead of dragging the receivers off with it.
    ErrorCounters node;
    node.on(ErrorEvent::ReceiveError);
    EXPECT_EQ(node.receive_errors(), 1U);
}

TEST(ErrorCounters, AnErrorAfterOurOwnFlagCostsEight) {
    ErrorCounters node;
    node.on(ErrorEvent::ReceiveErrorAfterFlag);
    EXPECT_EQ(node.receive_errors(), 8U);
}

TEST(ErrorCounters, ItTakesEightGoodFramesToUndoOneBadOne) {
    ErrorCounters node;
    node.on(ErrorEvent::TransmitError);
    repeat(node, ErrorEvent::TransmitSuccess, 7);
    EXPECT_EQ(node.transmit_errors(), 1U) << "still not clean";
    node.on(ErrorEvent::TransmitSuccess);
    EXPECT_EQ(node.transmit_errors(), 0U);
}

TEST(ErrorCounters, SuccessNeverDrivesACounterBelowZero) {
    ErrorCounters node;
    repeat(node, ErrorEvent::TransmitSuccess, 10);
    repeat(node, ErrorEvent::ReceiveSuccess, 10);
    EXPECT_EQ(node.transmit_errors(), 0U);
    EXPECT_EQ(node.receive_errors(), 0U);
}

// --- Active / passive boundary ---------------------------------------------

TEST(ErrorCounters, OneHundredTwentySevenIsStillActive) {
    ErrorCounters node;
    repeat(node, ErrorEvent::TransmitError, 16);  // 16 * 8 = 128
    ASSERT_EQ(node.transmit_errors(), 128U);
    ASSERT_EQ(node.state(), ErrorState::Passive);

    node.on(ErrorEvent::TransmitSuccess);  // back to 127
    EXPECT_EQ(node.transmit_errors(), kErrorPassiveLimit);
    EXPECT_EQ(node.state(), ErrorState::Active) << "the threshold is exclusive";
}

TEST(ErrorCounters, AHighReceiveCounterAloneMakesTheNodePassive) {
    ErrorCounters node;
    repeat(node, ErrorEvent::ReceiveError, 200);
    EXPECT_EQ(node.state(), ErrorState::Passive);
    EXPECT_EQ(node.transmit_errors(), 0U);
}

TEST(ErrorCounters, OneCleanFrameRescuesAnErrorPassiveReceiver) {
    // The spec deliberately snaps REC back to the boundary rather than making
    // a receiver count down one frame at a time from 250.
    ErrorCounters node;
    repeat(node, ErrorEvent::ReceiveError, 200);
    ASSERT_EQ(node.state(), ErrorState::Passive);

    node.on(ErrorEvent::ReceiveSuccess);
    EXPECT_EQ(node.receive_errors(), kErrorPassiveLimit);
    EXPECT_EQ(node.state(), ErrorState::Active);
}

// --- Bus off ---------------------------------------------------------------

TEST(ErrorCounters, ThirtyTwoFailedTransmissionsEndInBusOff) {
    ErrorCounters node;
    repeat(node, ErrorEvent::TransmitError, 31);  // 248
    ASSERT_EQ(node.state(), ErrorState::Passive) << "close, but still talking";

    node.on(ErrorEvent::TransmitError);  // 256
    EXPECT_EQ(node.state(), ErrorState::BusOff);
    EXPECT_EQ(node.transmit_errors(), kBusOffLimit);
}

TEST(ErrorCounters, ReceiveErrorsNeverCauseBusOff) {
    // The single most useful fact here: a listen-only node cannot be knocked
    // off the bus by a storm of errors, however long it lasts.
    ErrorCounters node;
    repeat(node, ErrorEvent::ReceiveErrorAfterFlag, 1000);
    EXPECT_EQ(node.state(), ErrorState::Passive);
    EXPECT_LE(node.receive_errors(), kBusOffLimit) << "the counter saturates, it does not wrap";
}

TEST(ErrorCounters, BusOffFreezesTheCounters) {
    ErrorCounters node;
    repeat(node, ErrorEvent::TransmitError, 32);
    ASSERT_EQ(node.state(), ErrorState::BusOff);

    // The node is electrically off the bus: it cannot transmit successfully,
    // so nothing can quietly walk TEC back down and put it on again.
    repeat(node, ErrorEvent::TransmitSuccess, 500);
    repeat(node, ErrorEvent::ReceiveSuccess, 500);
    EXPECT_EQ(node.state(), ErrorState::BusOff);
    EXPECT_EQ(node.transmit_errors(), kBusOffLimit);
}

TEST(ErrorCounters, RecoveryTakesOneHundredTwentyEightQuietWindows) {
    ErrorCounters node;
    repeat(node, ErrorEvent::TransmitError, 32);
    ASSERT_EQ(node.state(), ErrorState::BusOff);

    repeat(node, ErrorEvent::RecessiveWindow, kRecoveryWindows - 1U);
    EXPECT_EQ(node.state(), ErrorState::BusOff);
    EXPECT_EQ(node.recovery_windows(), kRecoveryWindows - 1U);

    node.on(ErrorEvent::RecessiveWindow);
    EXPECT_EQ(node.state(), ErrorState::Active);
    EXPECT_EQ(node.transmit_errors(), 0U);
    EXPECT_EQ(node.receive_errors(), 0U);
    EXPECT_EQ(node.recovery_windows(), 0U);
}

TEST(ErrorCounters, QuietWindowsDoNothingWhileTheNodeIsOnTheBus) {
    ErrorCounters node;
    repeat(node, ErrorEvent::RecessiveWindow, 500);
    EXPECT_EQ(node.state(), ErrorState::Active);
    EXPECT_EQ(node.recovery_windows(), 0U);
}

// --- Housekeeping ----------------------------------------------------------

TEST(ErrorCounters, ResetReturnsToPowerOn) {
    ErrorCounters node;
    repeat(node, ErrorEvent::TransmitError, 32);
    ASSERT_EQ(node.state(), ErrorState::BusOff);

    node.reset();
    EXPECT_EQ(node.state(), ErrorState::Active);
    EXPECT_EQ(node.transmit_errors(), 0U);
    EXPECT_EQ(node.receive_errors(), 0U);
}

TEST(ErrorState, HasAGreppableName) {
    EXPECT_EQ(to_string(ErrorState::Active), "active");
    EXPECT_EQ(to_string(ErrorState::Passive), "passive");
    EXPECT_EQ(to_string(ErrorState::BusOff), "bus-off");
}

}  // namespace
