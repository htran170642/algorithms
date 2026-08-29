#include "av/can/arbitration.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "av/can/frame.hpp"

namespace av::can {
namespace {

/// One name per wire position, in the 29-bit numbering. See the header for why
/// position 11 carries two names.
constexpr std::array<std::string_view, kExtendedArbitrationBits> kBitNames{
    "ID28", "ID27", "ID26", "ID25", "ID24", "ID23", "ID22", "ID21", "ID20", "ID19", "ID18",
    "RTR/SRR", "IDE",
    "ID17", "ID16", "ID15", "ID14", "ID13", "ID12", "ID11", "ID10", "ID9", "ID8", "ID7",
    "ID6", "ID5", "ID4", "ID3", "ID2", "ID1", "ID0",
    "RTR",
};

/// A 1 in the identifier is a recessive bit on the wire. Getting this mapping
/// backwards is the fastest way to "prove" that higher ids win.
constexpr BitValue level_of(std::uint32_t word, unsigned bit) noexcept {
    return ((word >> bit) & 1U) != 0U ? BitValue::Recessive : BitValue::Dominant;
}

}  // namespace

std::string_view arbitration_bit_name(std::size_t index) noexcept {
    if (index >= kBitNames.size()) {
        return {};
    }
    return kBitNames[index];
}

std::vector<BitValue> arbitration_field(const Contender& contender) {
    // A remote frame asks for data instead of carrying it, and drives RTR
    // recessive. So a data frame always beats a remote frame of the same id:
    // supplying the value outranks asking for it.
    const BitValue rtr = contender.remote ? BitValue::Recessive : BitValue::Dominant;

    std::vector<BitValue> bits;

    if (!contender.extended) {
        bits.reserve(kStandardArbitrationBits);
        for (unsigned i = 11U; i-- > 0U;) {
            bits.push_back(level_of(contender.id, i));
        }
        bits.push_back(rtr);
        bits.push_back(BitValue::Dominant);  // IDE: the identifier was 11 bits
        return bits;
    }

    bits.reserve(kExtendedArbitrationBits);
    for (unsigned i = 29U; i-- > 18U;) {  // ID28..ID18, the base identifier
        bits.push_back(level_of(contender.id, i));
    }
    bits.push_back(BitValue::Recessive);  // SRR, always
    bits.push_back(BitValue::Recessive);  // IDE: 18 more identifier bits follow
    for (unsigned i = 18U; i-- > 0U;) {   // ID17..ID0
        bits.push_back(level_of(contender.id, i));
    }
    bits.push_back(rtr);
    return bits;
}

std::optional<ArbitrationResult> arbitrate(const std::vector<Contender>& contenders) {
    if (contenders.empty()) {
        return std::nullopt;
    }

    std::vector<std::vector<BitValue>> fields;
    fields.reserve(contenders.size());
    for (const auto& contender : contenders) {
        const std::uint32_t mask = contender.extended ? kExtendedIdMask : kStandardIdMask;
        if ((contender.id & ~mask) != 0U) {
            return std::nullopt;
        }
        fields.push_back(arbitration_field(contender));
    }

    ArbitrationResult result;
    result.winners.resize(contenders.size());
    std::iota(result.winners.begin(), result.winners.end(), std::size_t{0});

    // Only positions every contender actually drives can be compared, so a
    // standard frame caps the round at 13 bits. That never truncates a live
    // race: by bit 12 a standard frame has already beaten every extended frame
    // sharing its base id, and two standard frames are either decided by then
    // or identical.
    std::size_t limit = kExtendedArbitrationBits;
    for (const auto& field : fields) {
        limit = std::min(limit, field.size());
    }

    for (std::size_t i = 0; i < limit && result.winners.size() > 1U; ++i) {
        ArbitrationStep step;
        step.index = i;
        step.name = arbitration_bit_name(i);

        // Wired-AND: one dominant driver pulls the whole bus down.
        step.bus = BitValue::Recessive;
        for (const std::size_t node : result.winners) {
            if (fields[node][i] == BitValue::Dominant) {
                step.bus = BitValue::Dominant;
                break;
            }
        }

        // Every node reads the bus back. Sending recessive and reading dominant
        // is the loss condition -- and the node learns it before it has put a
        // single wrong bit on the wire, which is what "non-destructive" means.
        std::vector<std::size_t> still_in;
        still_in.reserve(result.winners.size());
        for (const std::size_t node : result.winners) {
            if (fields[node][i] == step.bus) {
                still_in.push_back(node);
            } else {
                step.lost.push_back(node);
            }
        }

        result.winners = std::move(still_in);
        result.trace.push_back(std::move(step));
    }

    return result;
}

}  // namespace av::can
