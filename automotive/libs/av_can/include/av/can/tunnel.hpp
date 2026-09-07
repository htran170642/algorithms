#pragma once

// CAN frames packed into one UDP datagram, with a sequence number.
//
// Week 5 put a CanFrame on a wire that guaranteed order and delivery. Week 7
// puts the same frame on a wire that guarantees neither, so something has to
// carry the missing information. That something is this header:
//
//     ┌─ 16 bytes ──────────────────────────────────────────────┐
//     │ 'A' 'V' 'E' 'T' │ ver │ count │ rsvd │ sequence │ ms     │
//     └─────────────────────────────────────────────────────────┘
//     ┌─ 16 bytes per frame, `count` of them ───────────────────┐
//     │ id (bit31 = extended) │ len │ flags │ rsvd │ 8 data     │
//     └─────────────────────────────────────────────────────────┘
//
// `sequence` is the whole point. Nothing else in the stack can tell a receiver
// that a datagram is missing, or that the one in its hand is older than the one
// it already processed. A switch will not say so, UDP will not say so, and the
// kernel will not say so.
//
// Three deliberate choices worth defending:
//
//   1. **Big-endian on the wire.** Network byte order, so a big-endian ECU and
//      a little-endian one agree without negotiating. The CAN payload bytes are
//      copied verbatim -- their meaning belongs to the DBC, not to this layer.
//   2. **Batching.** One datagram carries up to kMaxFramesPerDatagram frames.
//      At 10 Hz with 2 messages the saving is trivial; at 1 kHz it is the
//      difference between 1000 and 16 datagrams a second, and every datagram
//      costs a full IP + UDP header and one trip through the switch.
//   3. **8 payload bytes per record, not 64.** This format cannot carry a
//      CAN-FD frame. That is a real limitation, stated rather than hidden: it
//      keeps the record a flat 16 bytes for week 7, and week 8 replaces the
//      whole thing with SOME/IP serialisation anyway.
//
// The bit-31 extended flag is the same convention week 6 met in the DBC's BO_
// line. Reusing it is not laziness -- it means one rule to remember, and the
// same off-by-one bug to recognise in two places.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "av/can/frame.hpp"

namespace av::can {

/// 'A' 'V' 'E' 'T' -- AV Ethernet Tunnel. A datagram that does not start with
/// it is not ours, and on a shared multicast group that happens.
inline constexpr std::uint32_t kTunnelMagic = 0x4156'4554U;
inline constexpr std::uint8_t kTunnelVersion = 1;

inline constexpr std::size_t kTunnelHeaderSize = 16;
inline constexpr std::size_t kTunnelRecordSize = 16;

/// 16 + 64*16 = 1040 bytes, comfortably under av::eth::kMaxDatagram (1472), so
/// a full batch still never fragments.
inline constexpr std::size_t kMaxFramesPerDatagram = 64;

struct TunnelHeader {
    std::uint32_t sequence{0};      ///< +1 per datagram sent, wraps at 2^32
    std::uint32_t timestamp_ms{0};  ///< sender's monotonic clock, wraps at ~49 days
    std::uint8_t count{0};          ///< frames in this datagram
};

/// Serialises `frames` into `buffer`, replacing its contents.
///
/// Returns false if there are no frames, too many, or one of them is invalid.
/// A partially-built datagram is never emitted: a receiver cannot tell a short
/// batch from a truncated one, so the sender must not create the ambiguity.
bool tunnel_encode(const TunnelHeader& header, const std::vector<CanFrame>& frames,
                   std::vector<std::uint8_t>& buffer);

struct TunnelPacket {
    TunnelHeader header;
    std::vector<CanFrame> frames;
};

/// Parses one datagram. Returns nullopt for anything that is not a well-formed
/// packet of this version -- wrong magic, wrong version, or a length that does
/// not match `count`.
///
/// Note what is *not* checked here: whether the sequence number makes sense.
/// That is not a property of one datagram, only of a stream of them, so it
/// belongs to the receiver. See SequenceTracker.
[[nodiscard]] std::optional<TunnelPacket> tunnel_decode(const std::uint8_t* data,
                                                        std::size_t size);

/// What a receiver learns by comparing consecutive sequence numbers.
enum class PacketOrder : std::uint8_t {
    First,      ///< nothing to compare against yet
    InOrder,    ///< exactly the expected sequence
    Gap,        ///< newer than expected: datagrams were lost on the way
    Stale,      ///< older than one already accepted: reordered, must be dropped
    Duplicate,  ///< this sequence has been seen before
};

/// The three lines of state that turn "bytes arrived" into "bytes arrived in a
/// usable order". Without it a receiver silently applies a stale value and the
/// displayed speed walks backwards -- with no error anywhere in the system.
class SequenceTracker {
public:
    PacketOrder observe(std::uint32_t sequence) noexcept;

    [[nodiscard]] std::uint64_t accepted() const noexcept { return accepted_; }
    [[nodiscard]] std::uint64_t lost() const noexcept { return lost_; }
    [[nodiscard]] std::uint64_t stale() const noexcept { return stale_; }

private:
    std::uint32_t highest_{0};
    bool started_{false};
    std::uint64_t accepted_{0};
    std::uint64_t lost_{0};
    std::uint64_t stale_{0};
};

}  // namespace av::can
