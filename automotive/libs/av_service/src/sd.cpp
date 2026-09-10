#include "av/service/sd.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "av/log.hpp"
#include "av/service/someip.hpp"

namespace av::service::sd {
namespace {

namespace log = av::log;

/// What an IPv4 endpoint option declares in its Length field.
///
/// **Not 12, which is what the option occupies on the wire.** The field counts
/// neither itself nor the Type byte that follows it:
///
///     length(2) + type(1)  excluded
///     reserved(1) + address(4) + reserved(1) + protocol(1) + port(2)  =  9
///
/// So an option is `declared + 3` bytes long. This is the second time in two
/// weeks that a length field measures something other than the structure it
/// introduces -- SOME/IP's counts from the Request ID, and this one skips its
/// own header. Neither is a mistake in the standard; both are mistakes in
/// every first implementation of it, and both are invisible until a second
/// stack appears, because one option parses fine and only the *next* one is
/// misaligned.
constexpr std::uint16_t kIpv4OptionLength = 9;

/// Flags byte, bit 7 and bit 6.
constexpr std::uint8_t kFlagReboot = 0x80;
constexpr std::uint8_t kFlagUnicast = 0x40;

/// Smallest possible payload: flags+reserved (4), entries length (4), options
/// length (4). An SD message with no entries is legal and useless, but it must
/// still carry both array lengths.
constexpr std::size_t kMinPayload = 12;

/// The index/count fields are one byte and half a byte respectively, so an
/// entry cannot reference more than 15 options in its first run.
constexpr std::size_t kMaxOptionsPerEntry = 15;
constexpr std::size_t kMaxOptions = 255;

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

/// TTL is 24 bits: three bytes, big-endian, with no room for a fourth.
void append_u24(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

std::uint32_t read_u24(const std::uint8_t* data) noexcept {
    return (static_cast<std::uint32_t>(data[0]) << 16U) |
           (static_cast<std::uint32_t>(data[1]) << 8U) | static_cast<std::uint32_t>(data[2]);
}

bool is_service_entry(EntryType type) noexcept {
    return type == EntryType::FindService || type == EntryType::OfferService;
}

bool known_entry_type(std::uint8_t raw) noexcept {
    switch (raw) {
        case static_cast<std::uint8_t>(EntryType::FindService):
        case static_cast<std::uint8_t>(EntryType::OfferService):
        case static_cast<std::uint8_t>(EntryType::SubscribeEventgroup):
        case static_cast<std::uint8_t>(EntryType::SubscribeEventgroupAck):
            return true;
        default:
            return false;
    }
}

bool valid_ipv4(const std::string& address) noexcept {
    in_addr parsed{};
    return inet_pton(AF_INET, address.c_str(), &parsed) == 1;
}

void append_ipv4(std::vector<std::uint8_t>& out, const std::string& address) {
    in_addr parsed{};
    static_cast<void>(inet_pton(AF_INET, address.c_str(), &parsed));
    // s_addr is already in network byte order. Converting to host order and
    // writing byte by byte keeps the wire independent of how this platform
    // happens to lay the struct out.
    append_u32(out, ntohl(parsed.s_addr));
}

std::string read_ipv4(const std::uint8_t* data) {
    std::array<char, INET_ADDRSTRLEN> text{};
    in_addr parsed{};
    parsed.s_addr = htonl(read_u32(data));
    if (inet_ntop(AF_INET, &parsed, text.data(), text.size()) == nullptr) {
        return {};
    }
    return std::string{text.data()};
}

void append_option(std::vector<std::uint8_t>& out, const Option& option) {
    append_u16(out, kIpv4OptionLength);
    out.push_back(static_cast<std::uint8_t>(option.type));
    out.push_back(0);  // reserved
    append_ipv4(out, option.address);
    out.push_back(0);  // reserved
    out.push_back(static_cast<std::uint8_t>(option.protocol));
    append_u16(out, option.port);
}

void append_entry(std::vector<std::uint8_t>& out, const Entry& entry, std::size_t option_index,
                  std::size_t option_count) {
    out.push_back(static_cast<std::uint8_t>(entry.type));
    out.push_back(static_cast<std::uint8_t>(option_index));
    out.push_back(0);  // index of the second option run -- unused here
    // High nibble counts the first run, low nibble the second. Two four-bit
    // counters packed into one byte is where a hand-written encoder usually
    // gets it backwards, and the symptom is a peer that reads the endpoints
    // belonging to some other entry.
    out.push_back(static_cast<std::uint8_t>((option_count & 0x0FU) << 4U));
    append_u16(out, entry.service_id);
    append_u16(out, entry.instance_id);
    out.push_back(entry.major_version);
    append_u24(out, entry.ttl);

    if (is_service_entry(entry.type)) {
        append_u32(out, entry.minor_version);
    } else {
        out.push_back(0);  // reserved
        out.push_back(0);  // reserved nibble + counter nibble
        append_u16(out, entry.eventgroup_id);
    }
}

/// Reads the options array into a flat list, skipping option types this
/// implementation does not know.
///
/// Skipping rather than failing is deliberate: SD is extensible, and a stack
/// that refuses a whole message because it contains one configuration option it
/// has never heard of stops working the day a supplier updates their ECU.
bool parse_options(const std::uint8_t* data, std::size_t size, std::vector<Option>& out) {
    std::size_t offset = 0;
    while (offset < size) {
        if (size - offset < 4) {
            log::warn("sd", "options array ends inside an option header");
            return false;
        }
        const std::uint16_t declared = read_u16(data + offset);
        const std::uint8_t type = data[offset + 2];
        // + 3: the two length bytes and the type byte the field does not count.
        // Get this wrong and every option after the first is misaligned.
        const std::size_t total = static_cast<std::size_t>(declared) + 3;
        if (total < 4 || size - offset < total) {
            log::warn("sd", "option length runs past the array", "declared", declared);
            return false;
        }

        if (type == static_cast<std::uint8_t>(OptionType::Ipv4Endpoint) ||
            type == static_cast<std::uint8_t>(OptionType::Ipv4Multicast)) {
            if (declared != kIpv4OptionLength) {
                log::warn("sd", "IPv4 option with the wrong length", "declared", declared,
                          "expected", unsigned{kIpv4OptionLength});
                return false;
            }
            Option option;
            option.type = static_cast<OptionType>(type);
            option.address = read_ipv4(data + offset + 4);
            option.protocol = static_cast<Layer4>(data[offset + 9]);
            option.port = read_u16(data + offset + 10);
            out.push_back(std::move(option));
        } else {
            // Unknown but well-formed. Keep a placeholder so that the index an
            // entry refers to still counts to the right place.
            out.emplace_back();
        }
        offset += total;
    }
    return true;
}

bool parse_entry(const std::uint8_t* data, const std::vector<Option>& options, Entry& out) {
    if (!known_entry_type(data[0])) {
        log::warn("sd", "unknown entry type -- ignored", "type", unsigned{data[0]});
        return false;
    }
    out.type = static_cast<EntryType>(data[0]);
    const std::size_t index = data[1];
    const std::size_t count = (data[3] >> 4U) & 0x0FU;
    out.service_id = read_u16(data + 4);
    out.instance_id = read_u16(data + 6);
    out.major_version = data[8];
    out.ttl = read_u24(data + 9);

    if (is_service_entry(out.type)) {
        out.minor_version = read_u32(data + 12);
    } else {
        out.eventgroup_id = read_u16(data + 14);
    }

    if (count == 0) {
        return true;
    }
    if (index + count > options.size()) {
        log::warn("sd", "entry references options that are not there", "index", index, "count",
                  count, "available", options.size());
        return false;
    }
    out.options.assign(options.begin() + static_cast<std::ptrdiff_t>(index),
                       options.begin() + static_cast<std::ptrdiff_t>(index + count));
    return true;
}

}  // namespace

bool is_sd(const service::Header& header) noexcept {
    return header.service_id == kServiceId && header.method_id == kMethodId;
}

bool serialize(const Message& message, std::uint16_t session_id, std::vector<std::uint8_t>& out) {
    std::vector<std::uint8_t> entries;
    std::vector<std::uint8_t> options;
    std::size_t option_count = 0;

    for (const auto& entry : message.entries) {
        if (entry.options.size() > kMaxOptionsPerEntry ||
            option_count + entry.options.size() > kMaxOptions) {
            log::error("sd", "too many options to index", "entry_options", entry.options.size(),
                       "total", option_count);
            out.clear();
            return false;
        }
        const std::size_t index = option_count;
        for (const auto& option : entry.options) {
            if (!valid_ipv4(option.address)) {
                log::error("sd", "not an IPv4 address", "address", option.address);
                out.clear();
                return false;
            }
            append_option(options, option);
            ++option_count;
        }
        append_entry(entries, entry, index, entry.options.size());
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(kMinPayload + entries.size() + options.size());

    std::uint8_t flags = 0;
    if (message.reboot) {
        flags |= kFlagReboot;
    }
    if (message.unicast) {
        flags |= kFlagUnicast;
    }
    payload.push_back(flags);
    payload.push_back(0);
    payload.push_back(0);
    payload.push_back(0);

    append_u32(payload, static_cast<std::uint32_t>(entries.size()));
    payload.insert(payload.end(), entries.begin(), entries.end());
    append_u32(payload, static_cast<std::uint32_t>(options.size()));
    payload.insert(payload.end(), options.begin(), options.end());

    // SD travels as an ordinary SOME/IP Notification. Nothing about the header
    // is special-cased -- which is the point: a stack that can parse week 8's
    // messages can already *receive* discovery, and only the payload is new.
    service::Header header;
    header.service_id = kServiceId;
    header.method_id = kMethodId;
    header.client_id = 0x0000;
    header.session_id = session_id;
    header.interface_version = kInterfaceVersion;
    header.type = service::MessageType::Notification;
    header.code = service::ReturnCode::Ok;
    return service::serialize(header, payload, out);
}

std::optional<Message> parse_payload(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < kMinPayload) {
        return std::nullopt;
    }

    const std::uint32_t entries_length = read_u32(data + 4);
    if (entries_length % kEntrySize != 0) {
        log::warn("sd", "entries array is not a whole number of entries", "bytes", entries_length);
        return std::nullopt;
    }
    // 8 for the flags/reserved word and the entries length, then the entries,
    // then 4 more that must be there to hold the options length.
    if (size < 8 + entries_length + 4) {
        log::warn("sd", "entries array runs past the payload", "declared", entries_length, "have",
                  size);
        return std::nullopt;
    }

    const std::size_t options_at = 8 + entries_length;
    const std::uint32_t options_length = read_u32(data + options_at);
    if (size != options_at + 4 + options_length) {
        log::warn("sd", "options array disagrees with the payload size", "declared", options_length,
                  "have", size - options_at - 4);
        return std::nullopt;
    }

    std::vector<Option> options;
    if (!parse_options(data + options_at + 4, options_length, options)) {
        return std::nullopt;
    }

    Message message;
    message.reboot = (data[0] & kFlagReboot) != 0;
    message.unicast = (data[0] & kFlagUnicast) != 0;
    for (std::size_t offset = 0; offset < entries_length; offset += kEntrySize) {
        Entry entry;
        if (parse_entry(data + 8 + offset, options, entry)) {
            message.entries.push_back(std::move(entry));
        }
    }
    return message;
}

Availability ServiceRegistry::observe(const Entry& entry, Clock::time_point now) {
    if (entry.type != EntryType::OfferService) {
        return Availability::Unchanged;
    }

    const std::uint32_t key = key_of(entry.service_id, entry.instance_id);
    const auto existing = records_.find(key);

    if (entry.ttl == kTtlStop) {
        // StopOffer. The server is going away and said so, which is the only
        // clean way a service disappears -- every other way is a lease running
        // out after the fact.
        if (existing == records_.end()) {
            return Availability::Unchanged;
        }
        records_.erase(existing);
        return Availability::Unavailable;
    }

    Record record;
    record.forever = (entry.ttl == kTtlForever);
    record.expires_at = now + std::chrono::seconds{entry.ttl};
    for (const auto& option : entry.options) {
        if (option.type == OptionType::Ipv4Multicast) {
            record.events = option;
        } else if (option.type == OptionType::Ipv4Endpoint) {
            record.method = option;
        }
    }

    const bool was_known = existing != records_.end();
    records_[key] = std::move(record);
    // A renewal is not news. Reporting Available on every cyclic offer would
    // make a client re-run its "the service came up" logic twice a second.
    return was_known ? Availability::Unchanged : Availability::Available;
}

std::vector<ServiceRegistry::Change> ServiceRegistry::expire(Clock::time_point now) {
    std::vector<Change> changes;
    for (auto it = records_.begin(); it != records_.end();) {
        if (it->second.forever || it->second.expires_at > now) {
            ++it;
            continue;
        }
        changes.push_back(Change{static_cast<std::uint16_t>(it->first >> 16U),
                                 static_cast<std::uint16_t>(it->first & 0xFFFFU),
                                 Availability::Unavailable});
        it = records_.erase(it);
    }
    return changes;
}

const ServiceRegistry::Record* ServiceRegistry::find(std::uint16_t service_id,
                                                     std::uint16_t instance_id) const {
    const auto it = records_.find(key_of(service_id, instance_id));
    return it == records_.end() ? nullptr : &it->second;
}

bool ServiceRegistry::available(std::uint16_t service_id, std::uint16_t instance_id) const {
    return find(service_id, instance_id) != nullptr;
}

std::optional<Option> ServiceRegistry::method_endpoint(std::uint16_t service_id,
                                                       std::uint16_t instance_id) const {
    const Record* record = find(service_id, instance_id);
    return record == nullptr ? std::nullopt : record->method;
}

std::optional<Option> ServiceRegistry::event_endpoint(std::uint16_t service_id,
                                                      std::uint16_t instance_id) const {
    const Record* record = find(service_id, instance_id);
    return record == nullptr ? std::nullopt : record->events;
}

}  // namespace av::service::sd
