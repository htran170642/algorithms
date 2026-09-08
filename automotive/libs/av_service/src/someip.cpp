#include "av/service/someip.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

#include "av/log.hpp"

namespace av::service {
namespace {

namespace log = av::log;

static_assert(sizeof(float) == 4, "SOME/IP float32 assumes a 4-byte IEEE 754 float");

void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

std::uint16_t read_u16(const std::uint8_t* data) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) |
                                      static_cast<std::uint16_t>(data[1]));
}

std::uint32_t read_u32(const std::uint8_t* data) noexcept {
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) | static_cast<std::uint32_t>(data[3]);
}

/// Only these five exist here. SOME/IP-TP and the ACK types would each need
/// their own handling, so accepting one silently would be worse than refusing
/// it -- the payload of a TP segment is not a whole message.
bool known_message_type(std::uint8_t raw) noexcept {
    switch (raw) {
        case static_cast<std::uint8_t>(MessageType::Request):
        case static_cast<std::uint8_t>(MessageType::RequestNoReturn):
        case static_cast<std::uint8_t>(MessageType::Notification):
        case static_cast<std::uint8_t>(MessageType::Response):
        case static_cast<std::uint8_t>(MessageType::Error):
            return true;
        default:
            return false;
    }
}

}  // namespace

bool serialize(const Header& header, const std::vector<std::uint8_t>& payload,
               std::vector<std::uint8_t>& out) {
    // The Length field is 32 bits and counts kLengthCovers plus the payload,
    // so the payload ceiling is smaller than the field's maximum by exactly
    // that much.
    const std::size_t limit =
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) - kLengthCovers;
    if (payload.size() > limit) {
        log::error("someip", "payload too large for the length field", "bytes", payload.size(),
                   "max", limit);
        out.clear();
        return false;
    }

    out.clear();
    out.reserve(kHeaderSize + payload.size());

    append_u16(out, header.service_id);
    append_u16(out, header.method_id);

    // Not the message size. Request ID onward: 8 + payload.
    append_u32(out, static_cast<std::uint32_t>(kLengthCovers + payload.size()));

    append_u16(out, header.client_id);
    append_u16(out, header.session_id);

    out.push_back(kProtocolVersion);
    out.push_back(header.interface_version);
    out.push_back(static_cast<std::uint8_t>(header.type));
    out.push_back(static_cast<std::uint8_t>(header.code));

    out.insert(out.end(), payload.begin(), payload.end());
    return true;
}

std::optional<Message> deserialize(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < kHeaderSize) {
        return std::nullopt;
    }

    const std::uint32_t length = read_u32(data + 4);
    if (length < kLengthCovers) {
        return std::nullopt;  // cannot even cover the fields it must
    }

    // The declared size and the delivered size must agree exactly.
    //
    // Over UDP a datagram is atomic, so a mismatch is a malformed message and
    // never a fragment awaiting more -- the same reasoning as week 7's tunnel.
    // Over TCP the caller would instead use `length` to find where this
    // message ends in a stream, which is what the field is really for.
    const std::size_t expected = kHeaderSize + (length - kLengthCovers);
    if (size != expected) {
        log::warn("someip", "length field disagrees with the bytes received", "declared", expected,
                  "received", size);
        return std::nullopt;
    }

    // Offsets 12 and 14, not 8 and 10. Bytes 8..11 are the Request ID, and
    // reading the protocol version out of a client id is the whole reason the
    // round-trip test pins every byte instead of trusting encode+decode to
    // agree with each other.
    if (data[12] != kProtocolVersion) {
        log::warn("someip", "unknown protocol version", "version", unsigned{data[12]}, "expected",
                  unsigned{kProtocolVersion});
        return std::nullopt;
    }
    if (!known_message_type(data[14])) {
        log::warn("someip", "unknown message type", "type", unsigned{data[14]});
        return std::nullopt;
    }

    Message message;
    message.header.service_id = read_u16(data);
    message.header.method_id = read_u16(data + 2);
    message.header.client_id = read_u16(data + 8);
    message.header.session_id = read_u16(data + 10);
    message.header.interface_version = data[13];
    message.header.type = static_cast<MessageType>(data[14]);
    message.header.code = static_cast<ReturnCode>(data[15]);

    message.payload.assign(data + kHeaderSize, data + size);
    return message;
}

std::uint16_t SessionCounter::next() noexcept {
    // 0 is reserved for "no session handling", so the counter runs 1..0xFFFF
    // and wraps back to 1 rather than to 0. Wrapping to 0 would silently
    // announce that this endpoint had stopped tracking sessions.
    if (value_ == std::numeric_limits<std::uint16_t>::max()) {
        value_ = 1;
    } else {
        ++value_;
    }
    return value_;
}

void PayloadWriter::put_u8(std::uint8_t value) { bytes_.push_back(value); }

void PayloadWriter::put_u16(std::uint16_t value) { append_u16(bytes_, value); }

void PayloadWriter::put_u32(std::uint32_t value) { append_u32(bytes_, value); }

void PayloadWriter::put_f32(float value) {
    // memcpy, not a reinterpret_cast through a uint32_t*: the cast would be a
    // strict-aliasing violation, and this compiles to the same move.
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    append_u32(bytes_, bits);
}

std::optional<std::uint8_t> PayloadReader::get_u8() noexcept {
    if (remaining() < 1) {
        return std::nullopt;
    }
    const std::uint8_t value = data_[offset_];
    offset_ += 1;
    return value;
}

std::optional<std::uint16_t> PayloadReader::get_u16() noexcept {
    if (remaining() < 2) {
        return std::nullopt;
    }
    const std::uint16_t value = read_u16(data_ + offset_);
    offset_ += 2;
    return value;
}

std::optional<std::uint32_t> PayloadReader::get_u32() noexcept {
    if (remaining() < 4) {
        return std::nullopt;
    }
    const std::uint32_t value = read_u32(data_ + offset_);
    offset_ += 4;
    return value;
}

std::optional<float> PayloadReader::get_f32() noexcept {
    const auto bits = get_u32();
    if (!bits) {
        return std::nullopt;
    }
    float value = 0.0F;
    const std::uint32_t raw = *bits;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

}  // namespace av::service
