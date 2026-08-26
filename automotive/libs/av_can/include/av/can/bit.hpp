#pragma once

// Bit-level access to a CAN payload, in the two layouts DBC files use.
//
// This is the bottom of the whole cockpit stack: every vehicle signal that
// ever reaches the cluster is first a run of bits somewhere inside eight (or
// sixty-four) bytes, and getting the walk wrong is the single most common
// source of "the number on screen is nonsense" bugs.

#include <cstddef>
#include <cstdint>
#include <optional>

namespace av::can {

/// How a multi-byte signal is laid out across the frame's bytes.
///
/// DBC writes these as `@1` (Intel) and `@0` (Motorola) on the `SG_` line.
enum class ByteOrder : std::uint8_t {
    Intel,     ///< little-endian: `start_bit` is the signal's *least* significant bit
    Motorola,  ///< big-endian:    `start_bit` is the signal's *most*  significant bit
};

/// A raw signal value is carried in a std::uint64_t, so this is the ceiling.
inline constexpr unsigned kMaxSignalBits = 64;

/// Reads `length` bits out of `data`, following `order`.
///
/// Bit numbering is DBC's: bit 0 is byte 0 mask 0x01, bit 7 is byte 0 mask
/// 0x80, bit 8 is byte 1 mask 0x01. Both layouts share this numbering; they
/// differ only in which direction the signal walks through it.
///
/// Returns nullopt when the signal does not fit inside `size` bytes, or when
/// `length` is 0 or above kMaxSignalBits. A caller that gets nullopt has a
/// malformed frame or a bad DBC row -- not a decoding result.
std::optional<std::uint64_t> extract_bits(const std::uint8_t* data, std::size_t size,
                                          unsigned start_bit, unsigned length,
                                          ByteOrder order) noexcept;

/// Writes the low `length` bits of `value` into `data`, following `order`.
///
/// Bits outside the signal are left untouched, so several signals can be
/// packed into one frame. Returns false on the same conditions extract_bits
/// returns nullopt, and in that case `data` is not modified at all.
bool insert_bits(std::uint8_t* data, std::size_t size, unsigned start_bit,
                 unsigned length, ByteOrder order, std::uint64_t value) noexcept;

/// Reinterprets a `length`-bit raw value as two's-complement signed.
///
/// DBC marks this with `-` instead of `+` after the byte order (`@1-`).
/// Forgetting it is why a steering angle of -1 degree reads as 6553.5.
std::int64_t sign_extend(std::uint64_t raw, unsigned length) noexcept;

}  // namespace av::can
