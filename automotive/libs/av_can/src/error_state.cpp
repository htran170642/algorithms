#include "av/can/error_state.hpp"

#include <cstdint>
#include <string_view>

namespace av::can {
namespace {

/// What one detected error costs. A success refunds 1, so the ratio is 8:1 and
/// a node failing more than one frame in eight climbs rather than settles.
constexpr std::uint16_t kErrorPenalty = 8;

}  // namespace

void ErrorCounters::on(ErrorEvent event) noexcept {
    if (bus_off_) {
        // Off the bus the node neither transmits nor receives, so nothing else
        // can move the counters. Only quiet-bus windows count.
        if (event == ErrorEvent::RecessiveWindow && windows_ < kRecoveryWindows) {
            ++windows_;
            if (windows_ == kRecoveryWindows) {
                reset();
            }
        }
        return;
    }

    switch (event) {
        case ErrorEvent::TransmitSuccess:
            if (tec_ > 0U) {
                --tec_;
            }
            break;

        case ErrorEvent::ReceiveSuccess:
            if (rec_ > kErrorPassiveLimit) {
                // The spec allows any value in 119..127 here. The point of the
                // rule is that one clean frame drops an error-passive receiver
                // straight back to the boundary instead of counting down from
                // 250 one frame at a time.
                rec_ = kErrorPassiveLimit;
            } else if (rec_ > 0U) {
                --rec_;
            }
            break;

        case ErrorEvent::TransmitError:
            tec_ = static_cast<std::uint16_t>(tec_ + kErrorPenalty);
            break;

        case ErrorEvent::ReceiveError:
            ++rec_;
            break;

        case ErrorEvent::ReceiveErrorAfterFlag:
            rec_ = static_cast<std::uint16_t>(rec_ + kErrorPenalty);
            break;

        case ErrorEvent::RecessiveWindow:
            // An idle bus while we are still on it: nothing to account for.
            break;
    }

    // REC can never cause bus-off, but a real controller's counter is only a
    // few bits wide. Saturating keeps the model from wrapping around into a
    // healthy-looking value after a long fault.
    if (rec_ > kBusOffLimit) {
        rec_ = kBusOffLimit;
    }

    if (tec_ >= kBusOffLimit) {
        // Latching, on purpose. Dropping back below 256 does not put the node
        // back on the bus -- only the 128-window recovery sequence does, and on
        // many controllers only after the application asks for it.
        tec_ = kBusOffLimit;
        bus_off_ = true;
        windows_ = 0U;
    }
}

ErrorState ErrorCounters::state() const noexcept {
    if (bus_off_) {
        return ErrorState::BusOff;
    }
    if (tec_ > kErrorPassiveLimit || rec_ > kErrorPassiveLimit) {
        return ErrorState::Passive;
    }
    return ErrorState::Active;
}

void ErrorCounters::reset() noexcept {
    tec_ = 0U;
    rec_ = 0U;
    windows_ = 0U;
    bus_off_ = false;
}

std::string_view to_string(ErrorState state) noexcept {
    switch (state) {
        case ErrorState::Active:
            return "active";
        case ErrorState::Passive:
            return "passive";
        case ErrorState::BusOff:
            return "bus-off";
    }
    return "unknown";
}

}  // namespace av::can
