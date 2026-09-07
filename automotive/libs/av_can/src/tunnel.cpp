#include "av/can/tunnel.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "av/can/frame.hpp"
#include "av/log.hpp"

namespace av::can {
namespace {

namespace log = av::log;

/// Bit 31 of the id word marks a 29-bit identifier -- the same convention the
/// DBC's BO_ line uses, met again in week 6.
constexpr std::uint32_t kExtendedBit = 0x8000'0000U;

constexpr std::uint8_t kFlagFd = 0x01U;
constexpr std::uint8_t kFlagBrs = 0x02U;

/// Big-endian, one byte at a time.
///
/// Not a memcpy of the host's uint32_t: that would put the bytes in whatever
/// order this CPU happens to use, and the receiver on the other ECU may not use
/// the same one. Shifting is endianness-independent by construction -- the code
/// says which byte goes first, so the wire does not depend on the machine that
/// wrote it.
void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

std::uint32_t read_u32(const std::uint8_t* data) noexcept {
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) | static_cast<std::uint32_t>(data[3]);
}

}  // namespace

bool tunnel_encode(const TunnelHeader& header, const std::vector<CanFrame>& frames,
                   std::vector<std::uint8_t>& buffer) {
    buffer.clear();

    if (frames.empty()) {
        log::error("can.tunnel", "refusing to send an empty batch");
        return false;
    }
    if (frames.size() > kMaxFramesPerDatagram) {
        log::error("can.tunnel", "batch too large", "frames", frames.size(), "max",
                   kMaxFramesPerDatagram);
        return false;
    }

    buffer.reserve(kTunnelHeaderSize + (frames.size() * kTunnelRecordSize));

    put_u32(buffer, kTunnelMagic);
    buffer.push_back(kTunnelVersion);
    buffer.push_back(static_cast<std::uint8_t>(frames.size()));
    buffer.push_back(0U);  // reserved: keeps sequence 4-byte aligned in the
    buffer.push_back(0U);  // datagram, and leaves room for flags later
    put_u32(buffer, header.sequence);
    put_u32(buffer, header.timestamp_ms);

    for (const auto& frame : frames) {
        if (!is_valid(frame)) {
            log::error("can.tunnel", "invalid frame in batch", "id", frame.id, "length",
                       unsigned{frame.length});
            buffer.clear();
            return false;
        }
        if (frame.length > kClassicMaxPayload) {
            // Stated, not silently truncated. A receiver given 8 of 64 bytes
            // would decode confident nonsense.
            log::error("can.tunnel", "CAN-FD payload does not fit this format", "id", frame.id,
                       "length", unsigned{frame.length}, "max", kClassicMaxPayload);
            buffer.clear();
            return false;
        }

        std::uint32_t id = frame.id & (frame.extended ? kExtendedIdMask : kStandardIdMask);
        if (frame.extended) {
            id |= kExtendedBit;
        }
        put_u32(buffer, id);

        buffer.push_back(frame.length);
        std::uint8_t flags = 0U;
        if (frame.fd) {
            flags |= kFlagFd;
        }
        if (frame.brs) {
            flags |= kFlagBrs;
        }
        buffer.push_back(flags);
        buffer.push_back(0U);  // reserved
        buffer.push_back(0U);

        // Always 8 bytes, zero-padded. A fixed-size record means a receiver can
        // find record N without walking records 0..N-1 -- and means a corrupt
        // length cannot desynchronise the rest of the datagram.
        for (std::size_t i = 0; i < kClassicMaxPayload; ++i) {
            buffer.push_back(i < static_cast<std::size_t>(frame.length) ? frame.data[i] : 0U);
        }
    }

    return true;
}

std::optional<TunnelPacket> tunnel_decode(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < kTunnelHeaderSize) {
        return std::nullopt;
    }
    if (read_u32(data) != kTunnelMagic) {
        return std::nullopt;  // somebody else's traffic on the same group
    }
    if (data[4] != kTunnelVersion) {
        return std::nullopt;
    }

    TunnelPacket packet;
    packet.header.count = data[5];
    packet.header.sequence = read_u32(data + 8);
    packet.header.timestamp_ms = read_u32(data + 12);

    if (packet.header.count == 0U || packet.header.count > kMaxFramesPerDatagram) {
        return std::nullopt;
    }

    // Exact, not "at least". A datagram is atomic -- one recvfrom() returns one
    // send_to() -- so a length that disagrees with the count is a malformed
    // packet, never a partial one waiting for its rest. This is the check a
    // TCP-shaped mental model gets wrong.
    const std::size_t expected =
        kTunnelHeaderSize + (static_cast<std::size_t>(packet.header.count) * kTunnelRecordSize);
    if (size != expected) {
        return std::nullopt;
    }

    packet.frames.reserve(packet.header.count);
    for (std::size_t n = 0; n < packet.header.count; ++n) {
        const std::uint8_t* record = data + kTunnelHeaderSize + (n * kTunnelRecordSize);

        CanFrame frame{};
        const std::uint32_t id = read_u32(record);
        frame.extended = (id & kExtendedBit) != 0U;
        frame.id = id & (frame.extended ? kExtendedIdMask : kStandardIdMask);
        frame.length = record[4];
        frame.fd = (record[5] & kFlagFd) != 0U;
        frame.brs = (record[5] & kFlagBrs) != 0U;

        if (frame.length > kClassicMaxPayload) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < static_cast<std::size_t>(frame.length); ++i) {
            frame.data[i] = record[8 + i];
        }
        if (!is_valid(frame)) {
            return std::nullopt;
        }

        packet.frames.push_back(frame);
    }

    return packet;
}

PacketOrder SequenceTracker::observe(std::uint32_t sequence) noexcept {
    if (!started_) {
        started_ = true;
        highest_ = sequence;
        ++accepted_;
        return PacketOrder::First;
    }

    // Wrap-safe comparison.
    //
    // `sequence < highest_` looks right and is wrong exactly once every 2^32
    // datagrams: at the wrap, sequence 0 compares as older than 4294967295 and
    // the receiver rejects everything from then on -- a gateway that ran long
    // enough silently stops being believed. Subtracting first lets the unsigned
    // arithmetic wrap the same way the counter does; the result cast to signed
    // is the true distance, forwards or backwards.
    const auto delta = static_cast<std::int32_t>(sequence - highest_);

    if (delta == 0) {
        ++stale_;
        return PacketOrder::Duplicate;
    }
    if (delta < 0) {
        // Reordered. Dropped rather than buffered: for a signal stream a newer
        // value has already superseded this one, so holding it back to restore
        // order would only display something older, later. A file transfer needs
        // the opposite policy -- which is why this decision belongs to the
        // application and not to the transport.
        ++stale_;
        return PacketOrder::Stale;
    }

    highest_ = sequence;
    ++accepted_;
    if (delta > 1) {
        lost_ += static_cast<std::uint64_t>(delta - 1);
        return PacketOrder::Gap;
    }
    return PacketOrder::InOrder;
}

}  // namespace av::can
