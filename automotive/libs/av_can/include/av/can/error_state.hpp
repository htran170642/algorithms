#pragma once

// CAN fault confinement: the error counters and the three-state machine every
// CAN controller runs in hardware.
//
// This is the mechanism that keeps one broken ECU from taking down a bus that
// twenty healthy ECUs depend on. It is also, for a cockpit, the difference
// between "the cluster is frozen" and "the cluster lost one signal" -- so it is
// worth understanding as a *diagnostic* tool, not only as protocol trivia.
//
//     Error Active   normal. Signals errors with 6 dominant bits, which
//                    deliberately destroys the frame on the bus so every node
//                    discards it together and the transmitter retries.
//
//     Error Passive  TEC > 127 or REC > 127. Still transmits and receives, but
//                    its error flag is 6 *recessive* bits, which nobody
//                    notices. It also waits 8 extra recessive bits before
//                    starting a transmission, so it always loses the race for
//                    an idle bus against a healthy node.
//
//     Bus Off        TEC >= 256. The node disconnects its transmitter. It is
//                    now electrically absent; the bus does not degrade at all.
//
// Two asymmetries carry the whole design, and both are interview material:
//
//   * An error costs 8, a success refunds 1. A node that fails one frame in
//     nine climbs toward bus-off instead of hovering. Intermittent faults are
//     not tolerated indefinitely -- they are escalated.
//
//   * The transmitter is charged 8, a receiver only 1. When an error appears,
//     the node that was talking is the most likely cause, so it is punished
//     eight times harder than the ones listening. That is why a single ECU
//     with a bad transceiver removes *itself*, rather than dragging every
//     receiver on the bus off with it.
//
// Consequence worth stating out loud: REC alone can never cause bus-off. A
// node that only listens tops out at error passive, however bad the bus gets.

#include <cstdint>
#include <string_view>

namespace av::can {

enum class ErrorState : std::uint8_t {
    Active,
    Passive,
    BusOff,
};

/// The events that move the counters, named after what the controller observed.
enum class ErrorEvent : std::uint8_t {
    TransmitSuccess,        ///< frame acknowledged: TEC - 1
    ReceiveSuccess,         ///< frame received cleanly: REC - 1
    TransmitError,          ///< error while transmitting: TEC + 8
    ReceiveError,           ///< error while receiving: REC + 1
    ReceiveErrorAfterFlag,  ///< dominant bit right after our error flag: REC + 8
    RecessiveWindow,        ///< 11 consecutive recessive bits; only counts when bus-off
};

/// A counter above this is error passive. 127 is still active.
inline constexpr std::uint16_t kErrorPassiveLimit = 127;

/// TEC at or above this is bus-off. REC never triggers it.
inline constexpr std::uint16_t kBusOffLimit = 256;

/// Bus-off recovery takes 128 windows of 11 recessive bits. At 500 kbit/s that
/// is roughly 2.8 ms of quiet bus -- fast enough that a cluster may never see
/// the gap, which is exactly what makes an intermittently recovering node hard
/// to diagnose from the application layer.
inline constexpr std::uint16_t kRecoveryWindows = 128;

/// One node's fault-confinement state. Not thread safe: a real controller runs
/// this in hardware, and the software mirror belongs to whichever thread owns
/// the interface.
class ErrorCounters {
public:
    void on(ErrorEvent event) noexcept;

    [[nodiscard]] ErrorState state() const noexcept;
    [[nodiscard]] std::uint16_t transmit_errors() const noexcept { return tec_; }
    [[nodiscard]] std::uint16_t receive_errors() const noexcept { return rec_; }

    /// Progress toward leaving bus-off, 0..kRecoveryWindows. Always 0 while the
    /// node is on the bus.
    [[nodiscard]] std::uint16_t recovery_windows() const noexcept { return windows_; }

    /// Back to a freshly powered controller.
    void reset() noexcept;

private:
    std::uint16_t tec_{0};
    std::uint16_t rec_{0};
    std::uint16_t windows_{0};
    bool bus_off_{false};
};

/// "active", "passive", "bus-off" -- for logs, so a grep across a trace finds
/// the transition without matching the enum's spelling in source.
std::string_view to_string(ErrorState state) noexcept;

}  // namespace av::can
