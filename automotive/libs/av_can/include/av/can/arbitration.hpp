#pragma once

// CAN bus arbitration, simulated one bit at a time.
//
// Week 1 started where SocketCAN starts: an id, a length, a payload. This file
// goes one layer lower, to the part of CAN that never reaches software but
// explains every priority decision the network makes.
//
// Two physical facts drive all of it:
//
//   1. The bus is wired-AND. A dominant bit (0) pulls the line down; a
//      recessive bit (1) merely lets it float. If any node on the bus drives
//      dominant, every node reads dominant.
//
//   2. A transmitting node reads back every bit it sends. A node that sends
//      recessive and reads dominant knows another node with a lower id is
//      talking, stops transmitting immediately, and becomes a receiver.
//
// Together they make arbitration *non-destructive*: the loser withdraws before
// it has corrupted anything, so the winner's frame is not damaged, not delayed
// and not retransmitted. Ethernet's CSMA/CD detects a collision and throws
// both frames away; CAN resolves the collision without one ever occurring.
//
// The consequence you must be able to state in an interview: a numerically
// lower identifier wins, because the id goes out most-significant bit first
// and 0 beats 1 at the first bit where two contenders differ.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace av::can {

/// A bus level. The numeric values are the logical bit, not a voltage:
/// dominant is 0, and it is the actively driven state.
enum class BitValue : std::uint8_t {
    Dominant = 0,
    Recessive = 1,
};

/// One node with one frame queued, at the instant the bus goes idle.
///
/// Deliberately not a CanFrame: arbitration only ever sees the id, the format
/// and the RTR bit, and a CanFrame carries no RTR because SocketCAN reports it
/// out of band. Modelling only what the arbitration field contains keeps the
/// simulation honest.
struct Contender {
    std::string_view name;  ///< ECU name, for the trace
    std::uint32_t id{};     ///< 11-bit, or 29-bit when `extended`
    bool extended{false};   ///< 29-bit identifier (CAN 2.0B)
    bool remote{false};     ///< remote frame: RTR recessive instead of dominant
};

/// Length of the arbitration field on the wire, in bits, excluding SOF.
///
/// A standard frame's field formally ends after RTR; the IDE bit that follows
/// belongs to its control field. It is counted here anyway, because on the wire
/// it sits at the same position as an extended frame's IDE, and that is
/// precisely what decides a standard-versus-extended contest.
inline constexpr std::size_t kStandardArbitrationBits = 13;
inline constexpr std::size_t kExtendedArbitrationBits = 32;

/// The bits `contender` drives, most significant first.
///
/// Standard (13 bits):
///     0..10  ID28..ID18   the 11-bit identifier
///     11     RTR          dominant for a data frame, recessive for remote
///     12     IDE          dominant: "this identifier was 11 bits"
///
/// Extended (32 bits):
///     0..10  ID28..ID18   the 11-bit *base* identifier
///     11     SRR          always recessive; stands in for the standard RTR
///     12     IDE          recessive: "18 more identifier bits follow"
///     13..30 ID17..ID0    the extension
///     31     RTR
///
/// Position 11 is the whole trick. A standard data frame drives it dominant
/// (RTR) while an extended frame must drive it recessive (SRR), so a standard
/// frame beats an extended frame that shares its base id -- and if the standard
/// frame is itself a remote frame the two tie there and position 12 decides it,
/// with the same outcome.
std::vector<BitValue> arbitration_field(const Contender& contender);

/// Name of wire position `index`, using the 29-bit numbering throughout.
///
/// A standard frame calls positions 0..10 ID10..ID0; they are the same bits.
/// Position 11 is labelled "RTR/SRR" because its meaning genuinely depends on
/// the format of the frame driving it. Returns an empty view past the end.
std::string_view arbitration_bit_name(std::size_t index) noexcept;

/// What happened at one wire position.
struct ArbitrationStep {
    std::size_t index{};                ///< position within the arbitration field
    std::string_view name;              ///< arbitration_bit_name(index)
    BitValue bus{BitValue::Recessive};  ///< the wired-AND of everyone still in
    std::vector<std::size_t> lost;      ///< contenders that withdrew on this bit
};

struct ArbitrationResult {
    /// Normally one entry. More than one means every remaining contender drove
    /// an identical arbitration field -- two ECUs configured with the same id,
    /// which arbitration cannot resolve. They both keep transmitting and then
    /// destroy each other in the data field. It is a configuration fault the
    /// bus surfaces as sporadic bit errors, never as a priority problem.
    std::vector<std::size_t> winners;
    std::vector<ArbitrationStep> trace;
};

/// Runs one arbitration round.
///
/// Returns nullopt when the input cannot describe a real bus: no contenders at
/// all, or an id with bits set outside its format's mask. Those are programming
/// errors, not bus events, and must not be reported as an arbitration outcome.
std::optional<ArbitrationResult> arbitrate(const std::vector<Contender>& contenders);

}  // namespace av::can
