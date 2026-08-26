#pragma once

// One CAN or CAN-FD frame, as software sees it.
//
// Deliberately not the wire format: SOF, CRC, ACK and bit stuffing are the
// controller's job and never reach the driver, let alone this code. What is
// left is what a SocketCAN read() actually hands you -- an id, a length, and
// the payload.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace av::can {

/// CAN-FD payload ceiling. Classic CAN stops at 8.
inline constexpr std::size_t kMaxPayload = 64;
inline constexpr std::size_t kClassicMaxPayload = 8;

/// 11-bit base identifier (CAN 2.0A) and 29-bit extended identifier (2.0B).
inline constexpr std::uint32_t kStandardIdMask = 0x7FFU;
inline constexpr std::uint32_t kExtendedIdMask = 0x1FFF'FFFFU;

struct CanFrame {
    std::uint32_t id{};                          ///< arbitration id, already masked
    std::uint8_t length{};                       ///< payload bytes present, not the DLC code
    bool extended{false};                        ///< 29-bit id instead of 11-bit
    bool fd{false};                              ///< CAN-FD frame
    bool brs{false};                             ///< bit-rate switch; FD only
    std::array<std::uint8_t, kMaxPayload> data{};
};

/// Turns the 4-bit on-the-wire DLC code into a byte count.
///
/// Up to 8 the code *is* the byte count. CAN-FD reuses codes 9..15 for the
/// jump to 12, 16, 20, 24, 32, 48 and 64 bytes -- which is why a CAN-FD
/// payload can never be, say, 13 bytes long. Classic CAN saturates at 8, so a
/// classic frame claiming DLC 15 still carries 8 bytes.
std::uint8_t dlc_to_length(std::uint8_t dlc, bool fd) noexcept;

/// The inverse. Returns nullopt for a length CAN-FD cannot encode.
std::optional<std::uint8_t> length_to_dlc(std::uint8_t length, bool fd) noexcept;

/// Checks the invariants a frame must hold before it can be sent or decoded:
/// the id fits its mask, the length is transmittable, and BRS implies FD.
bool is_valid(const CanFrame& frame) noexcept;

}  // namespace av::can
