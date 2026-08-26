#include "av/can/bit.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace av::can {
namespace {

/// Which byte of the payload holds DBC bit number `bit`.
constexpr std::size_t byte_index(std::size_t bit) noexcept { return bit / 8U; }

/// The mask selecting DBC bit number `bit` inside its byte.
constexpr std::uint8_t bit_mask(std::size_t bit) noexcept {
    return static_cast<std::uint8_t>(1U << (bit % 8U));
}

/// The bit that follows `bit` when walking a signal.
///
/// Intel simply counts up: 0, 1, 2, ... straight through the byte boundary.
///
/// Motorola runs from the MSB downward inside a byte, then continues at bit 7
/// of the *next* byte. In DBC numbering that is "minus one, except at a byte
/// boundary, where it is plus fifteen" -- because leaving byte k at its bit 0
/// (number 8k) and entering byte k+1 at its bit 7 (number 8k+15) is a jump of
/// exactly 15. That single line is the whole difference between the layouts.
constexpr std::size_t next_bit(std::size_t bit, ByteOrder order) noexcept {
    if (order == ByteOrder::Intel) {
        return bit + 1U;
    }
    return (bit % 8U == 0U) ? bit + 15U : bit - 1U;
}

/// Where bit `index` of the walk lands in the signal's raw value.
///
/// Intel walks LSB first, so step i carries value bit i. Motorola walks MSB
/// first, so step i carries value bit (length - 1 - i).
constexpr unsigned value_shift(unsigned index, unsigned length, ByteOrder order) noexcept {
    return (order == ByteOrder::Intel) ? index : (length - 1U - index);
}

/// Rejects the arguments that would make the walk meaningless or unsafe.
constexpr bool geometry_is_sane(std::size_t size, unsigned length) noexcept {
    return length != 0U && length <= kMaxSignalBits && size <= kMaxSignalBits;
}

}  // namespace

std::optional<std::uint64_t> extract_bits(const std::uint8_t* data, std::size_t size,
                                          unsigned start_bit, unsigned length,
                                          ByteOrder order) noexcept {
    if (data == nullptr || !geometry_is_sane(size, length)) {
        return std::nullopt;
    }

    const std::size_t total = size * 8U;
    std::uint64_t raw = 0U;
    std::size_t bit = start_bit;

    for (unsigned i = 0U; i < length; ++i) {
        // Checked every step, not once up front: a Motorola signal can start
        // inside the frame and still walk off the end.
        if (bit >= total) {
            return std::nullopt;
        }
        if ((data[byte_index(bit)] & bit_mask(bit)) != 0U) {
            raw |= std::uint64_t{1} << value_shift(i, length, order);
        }
        bit = next_bit(bit, order);
    }

    return raw;
}

bool insert_bits(std::uint8_t* data, std::size_t size, unsigned start_bit,
                 unsigned length, ByteOrder order, std::uint64_t value) noexcept {
    if (data == nullptr || !geometry_is_sane(size, length)) {
        return false;
    }

    const std::size_t total = size * 8U;

    // Walk once to validate before writing anything. A half-written signal is
    // worse than a rejected one: the receiver would decode it without error
    // and act on a value that was never sent.
    std::size_t bit = start_bit;
    for (unsigned i = 0U; i < length; ++i) {
        if (bit >= total) {
            return false;
        }
        bit = next_bit(bit, order);
    }

    bit = start_bit;
    for (unsigned i = 0U; i < length; ++i) {
        const std::uint8_t mask = bit_mask(bit);
        std::uint8_t& byte = data[byte_index(bit)];
        if (((value >> value_shift(i, length, order)) & 1U) != 0U) {
            byte = static_cast<std::uint8_t>(byte | mask);
        } else {
            byte = static_cast<std::uint8_t>(byte & static_cast<std::uint8_t>(~mask));
        }
        bit = next_bit(bit, order);
    }

    return true;
}

std::int64_t sign_extend(std::uint64_t raw, unsigned length) noexcept {
    // At 64 bits there is nothing to extend, and the masks below would shift
    // by 64, which is undefined behaviour.
    if (length == 0U || length >= kMaxSignalBits) {
        return static_cast<std::int64_t>(raw);
    }

    const std::uint64_t sign_bit = std::uint64_t{1} << (length - 1U);
    if ((raw & sign_bit) == 0U) {
        return static_cast<std::int64_t>(raw);
    }

    // Fill everything above the signal with ones. The cast of an out-of-range
    // unsigned is implementation-defined in C++17 (well-defined from C++20);
    // every target this project runs on is two's complement.
    const std::uint64_t fill = ~((std::uint64_t{1} << length) - 1U);
    return static_cast<std::int64_t>(raw | fill);
}

}  // namespace av::can
