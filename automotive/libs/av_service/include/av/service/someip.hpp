#pragma once

// The SOME/IP message header, and just enough serialisation to use it.
//
// Week 7 invented a wire format ('AVET') to get CAN frames across a network.
// It worked, and it was wrong in the way every in-house format is wrong: only
// this repository speaks it. SOME/IP is the format the industry already agreed
// on, and swapping to it is the point of week 8.
//
// The shift is not only in the bytes. It is in what a message *is*:
//
//     Week 4-7:   CAN ID  ──►  message  ──►  signal
//     Week 8:     service ──►  method / event / field
//
// A CAN id is a label on a broadcast. A SOME/IP message names a *service* and
// something to do with it, and carries who asked and which attempt this is.
//
//     ┌─ 16 bytes, big-endian throughout ──────────────────────────────┐
//     │  Message ID   │ Service ID (2) │ Method ID (2)                 │
//     │  Length       │ 4 bytes -- see the warning below               │
//     │  Request ID   │ Client ID (2)  │ Session ID (2)                │
//     │  Proto ver 1  │ Iface ver 1    │ Msg type 1 │ Return code 1    │
//     └────────────────────────────────────────────────────────────────┘
//
// **The Length field does not measure the message.** It counts from the
// Request ID onward: 8 header bytes plus the payload. So a 16-byte header with
// an empty payload carries Length 8, not 16 and not 0. Getting this wrong is
// the single most common SOME/IP implementation bug, and it stays invisible
// until two independently written stacks meet.
//
// **Everything is big-endian.** Note the collision with week 6: the DBC's `@1`
// signals are Intel byte order, so `dbc/cockpit.dbc` decodes VehicleSpeed
// little-endian while this header writes its Service ID big-endian -- opposite
// conventions, one process, and no compiler warning if they are confused.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace av::service {

inline constexpr std::size_t kHeaderSize = 16;

/// What the Length field counts on top of the payload: Request ID (4) plus the
/// four version/type/code bytes.
inline constexpr std::size_t kLengthCovers = 8;

inline constexpr std::uint8_t kProtocolVersion = 0x01;

/// Method ids with bit 15 set are events. The split is a convention of the
/// standard, not a separate field: 0x0000-0x7FFF are methods, 0x8000-0xFFFF
/// are events.
inline constexpr std::uint16_t kEventIdFlag = 0x8000U;

[[nodiscard]] constexpr bool is_event(std::uint16_t method_id) noexcept {
    return (method_id & kEventIdFlag) != 0U;
}

enum class MessageType : std::uint8_t {
    Request = 0x00,          ///< expects a Response
    RequestNoReturn = 0x01,  ///< fire and forget
    Notification = 0x02,     ///< an event, pushed to subscribers
    Response = 0x80,         ///< the answer to a Request
    Error = 0x81,            ///< the answer when it failed
};

enum class ReturnCode : std::uint8_t {
    Ok = 0x00,
    NotOk = 0x01,
    UnknownService = 0x02,
    UnknownMethod = 0x03,
    NotReady = 0x04,
    WrongProtocolVersion = 0x07,
    WrongInterfaceVersion = 0x08,
    WrongMessageType = 0x09,
};

struct Header {
    std::uint16_t service_id{0};
    std::uint16_t method_id{0};
    std::uint16_t client_id{0};
    /// Increments per request, wrapping 1..0xFFFF and never returning to 0.
    ///
    /// This is week 7's sequence number, promoted into the standard. 0x0000 is
    /// reserved to mean "this endpoint does not do session handling" -- which
    /// is exactly the naive receiver of week 7, except that the protocol now
    /// lets an endpoint *declare* it rather than leaving a reader to guess.
    std::uint16_t session_id{0};
    std::uint8_t interface_version{1};
    MessageType type{MessageType::Request};
    ReturnCode code{ReturnCode::Ok};
};

struct Message {
    Header header;
    std::vector<std::uint8_t> payload;
};

/// Writes header + payload into `out`, replacing its contents.
///
/// Fails only for a payload too large to describe in the 32-bit Length field.
bool serialize(const Header& header, const std::vector<std::uint8_t>& payload,
               std::vector<std::uint8_t>& out);

/// Parses one message.
///
/// Returns nullopt for a short buffer, an unknown protocol version, a message
/// type that is not one of the five, or a Length that disagrees with the bytes
/// actually present. That last check is the one that catches the Length bug
/// described at the top of this file -- on the receiving side, where it
/// presents as a peer that "sends garbage".
[[nodiscard]] std::optional<Message> deserialize(const std::uint8_t* data, std::size_t size);

/// Session ids, following the standard's wrap rule.
///
/// A separate type because "increment, but skip zero on wrap" is a rule that
/// gets forgotten the second time somebody writes `++session`.
class SessionCounter {
public:
    std::uint16_t next() noexcept;

private:
    std::uint16_t value_{0};
};

/// Appends fixed-width values in network byte order.
///
/// SOME/IP serialisation has more in it than this (strings, arrays, unions,
/// optional TLV members). These four cover a vehicle signal, which is what
/// weeks 8-10 need; the rest can arrive when something needs it.
class PayloadWriter {
public:
    void put_u8(std::uint8_t value);
    void put_u16(std::uint16_t value);
    void put_u32(std::uint32_t value);
    /// IEEE 754 single precision, big-endian -- the bit pattern, not a decimal
    /// rendering. Both ends must agree on a representation, and the standard
    /// picks this one.
    void put_f32(float value);

    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

/// Reads them back, refusing to run off the end.
///
/// Every getter returns optional rather than a value plus a separate error
/// flag: a truncated payload is a normal thing to receive from a network, and
/// a reader that returns 0 for "missing" is indistinguishable from one that
/// read a real 0.
class PayloadReader {
public:
    PayloadReader(const std::uint8_t* data, std::size_t size) noexcept
        : data_(data), size_(size) {}

    std::optional<std::uint8_t> get_u8() noexcept;
    std::optional<std::uint16_t> get_u16() noexcept;
    std::optional<std::uint32_t> get_u32() noexcept;
    std::optional<float> get_f32() noexcept;

    [[nodiscard]] std::size_t remaining() const noexcept { return size_ - offset_; }
    [[nodiscard]] bool exhausted() const noexcept { return offset_ == size_; }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t offset_{0};
};

}  // namespace av::service
