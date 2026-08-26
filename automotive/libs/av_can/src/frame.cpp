#include "av/can/frame.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <optional>

namespace av::can {
namespace {

/// Byte counts for DLC codes 9..15, the CAN-FD extension.
constexpr std::array<std::uint8_t, 7> kFdLengths{12U, 16U, 20U, 24U, 32U, 48U, 64U};
constexpr std::uint8_t kFirstFdDlc = 9U;
constexpr std::uint8_t kMaxDlc = 15U;

}  // namespace

std::uint8_t dlc_to_length(std::uint8_t dlc, bool fd) noexcept {
    if (dlc <= kClassicMaxPayload) {
        return dlc;
    }
    if (!fd || dlc > kMaxDlc) {
        // A classic controller transmits 8 bytes no matter what the code says.
        return static_cast<std::uint8_t>(kClassicMaxPayload);
    }
    return kFdLengths[dlc - kFirstFdDlc];
}

std::optional<std::uint8_t> length_to_dlc(std::uint8_t length, bool fd) noexcept {
    if (length <= kClassicMaxPayload) {
        return length;
    }
    if (!fd) {
        return std::nullopt;
    }

    const auto* const found = std::find(kFdLengths.begin(), kFdLengths.end(), length);
    if (found == kFdLengths.end()) {
        // 13 bytes, for instance: representable in memory, not on the bus.
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(kFirstFdDlc + std::distance(kFdLengths.begin(), found));
}

bool is_valid(const CanFrame& frame) noexcept {
    const std::uint32_t mask = frame.extended ? kExtendedIdMask : kStandardIdMask;
    if ((frame.id & ~mask) != 0U) {
        return false;
    }
    if (frame.brs && !frame.fd) {
        return false;
    }
    return length_to_dlc(frame.length, frame.fd).has_value();
}

}  // namespace av::can
