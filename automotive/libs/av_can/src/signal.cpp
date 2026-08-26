#include "av/can/signal.hpp"

#include <cmath>
#include <cstdint>
#include <optional>

#include "av/can/bit.hpp"
#include "av/can/frame.hpp"

namespace av::can {
namespace {

/// All ones in the low `length` bits. Split out because shifting a 64-bit
/// value by 64 is undefined behaviour, which is exactly the case a 64-bit
/// signal hits.
constexpr std::uint64_t low_mask(unsigned length) noexcept {
    return (length >= kMaxSignalBits) ? ~std::uint64_t{0}
                                      : (std::uint64_t{1} << length) - 1U;
}

}  // namespace

std::optional<double> decode(const SignalSpec& spec, const CanFrame& frame) noexcept {
    const auto raw = extract_bits(frame.data.data(), frame.length, spec.start_bit,
                                  spec.length, spec.order);
    if (!raw) {
        return std::nullopt;
    }

    const double value = spec.is_signed
                             ? static_cast<double>(sign_extend(*raw, spec.length))
                             : static_cast<double>(*raw);
    return (value * spec.factor) + spec.offset;
}

bool encode(const SignalSpec& spec, double physical, CanFrame& frame) noexcept {
    if (spec.length == 0U || spec.length > kMaxSignalBits) {
        return false;
    }
    if (spec.factor == 0.0 || !std::isfinite(physical)) {
        return false;
    }

    const double scaled = std::round((physical - spec.offset) / spec.factor);
    if (!std::isfinite(scaled)) {
        return false;
    }

    std::uint64_t raw = 0U;
    if (spec.is_signed) {
        // Two's complement holds [-2^(n-1), 2^(n-1)-1]. The upper bound is
        // written as a strict `>=` against 2^(n-1) rather than `> 2^(n-1)-1`
        // because at n = 64 that value is not representable as a double, and
        // the comparison would let through a number the cast cannot take.
        const double limit = std::ldexp(1.0, static_cast<int>(spec.length) - 1);
        if (scaled < -limit || scaled >= limit) {
            return false;
        }
        raw = static_cast<std::uint64_t>(static_cast<std::int64_t>(scaled)) & low_mask(spec.length);
    } else {
        const double limit = std::ldexp(1.0, static_cast<int>(spec.length));
        if (scaled < 0.0 || scaled >= limit) {
            return false;
        }
        raw = static_cast<std::uint64_t>(scaled);
    }

    return insert_bits(frame.data.data(), frame.length, spec.start_bit, spec.length,
                       spec.order, raw);
}

}  // namespace av::can
