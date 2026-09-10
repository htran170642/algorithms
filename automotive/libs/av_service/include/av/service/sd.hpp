#pragma once

// SOME/IP Service Discovery: the protocol that deletes the constants.
//
// Week 8 left the client knowing where the server was because a header said
// so:
//
//     inline constexpr std::uint16_t kMethodPort = 30509;      // vehicle_service.hpp
//     inline constexpr const char*   kEventGroup = "239.10.0.2";
//
// That works on one machine and fails on a vehicle, in three ordinary ways:
//
//     the ECU boots late     the client calls into a void and times out,
//                            with no way to tell "not yet" from "never"
//     the ECU moves          a different board, a different address, and a
//                            recompile of every client that talks to it
//     the ECU is not fitted  the base trim has no rear display, so the
//                            service genuinely does not exist -- and a
//                            timeout is the wrong way to learn that
//
// Service Discovery replaces the constant with a conversation:
//
//     server ──OfferService (cyclic, with a TTL)──► 239.10.0.9:30490
//     client ──FindService───────────────────────► 239.10.0.9:30490
//     server ──OfferService (unicast, right away)─► the client that asked
//
// The offer *carries the endpoints*, so the client learns the port and the
// event group instead of being compiled with them.
//
// **The TTL is the load-bearing part.** An offer is a lease, not a fact: it is
// valid for TTL seconds and the server must keep renewing it. That is what
// makes "the server died" observable at all -- a dead server stops renewing,
// the lease runs out, and every client sees the service go UNAVAILABLE without
// anyone having sent a message to say so. Week 8's timeout could not tell a
// dead server from a lost datagram; this can.
//
// SD is itself a SOME/IP message. Service ID 0xFFFF, Method ID 0x8100, message
// type Notification -- so week 8's serialize()/deserialize() carry it, and the
// header format needs no special case. The SD-specific part is the payload.
//
//     ┌─ SD payload ───────────────────────────────────────────────┐
//     │ Flags (1)   bit7 Reboot, bit6 Unicast    │ Reserved (3)     │
//     │ Length of Entries Array (4)                                 │
//     │ Entry [16] · Entry [16] · ...                               │
//     │ Length of Options Array (4)                                 │
//     │ Option [12] · Option [12] · ...                             │
//     └─────────────────────────────────────────────────────────────┘
//
// Entries and options are separate arrays because several entries commonly
// share one endpoint, and an entry then refers to options by *index and
// count* rather than repeating them. This header hides that indirection --
// each Entry owns its options in memory, and serialisation lays them into the
// shared array and fixes up the indices. The wire is unchanged; the API is
// smaller. See the note in sd.cpp for why the index/count pair is a classic
// interop bug.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "av/service/someip.hpp"

namespace av::service::sd {

/// SD's own "service". 0xFFFF is not a real service -- it is the escape hatch
/// that lets discovery ride the same header as everything else.
inline constexpr std::uint16_t kServiceId = 0xFFFF;
inline constexpr std::uint16_t kMethodId = 0x8100;
inline constexpr std::uint8_t kInterfaceVersion = 0x01;

/// The one address that still has to be agreed in advance. Discovery cannot
/// discover itself, so exactly one constant survives -- and it is the same for
/// every service on the vehicle, which is the difference that matters.
inline constexpr const char* kGroup = "239.10.0.9";
inline constexpr std::uint16_t kPort = 30490;

inline constexpr std::size_t kEntrySize = 16;
inline constexpr std::size_t kOptionSize = 12;

/// A lease that never expires. Legal, and a trap: with no renewal there is
/// nothing to stop, so a client can only learn the service is gone by failing
/// to reach it -- exactly the week 8 situation SD is meant to fix.
inline constexpr std::uint32_t kTtlForever = 0x00FF'FFFFU;

/// TTL 0 in an OfferService is a *StopOffer*: withdraw it now. This is the
/// polite shutdown -- a server that stops cleanly says so, and clients react in
/// milliseconds instead of waiting out the lease.
inline constexpr std::uint32_t kTtlStop = 0;

enum class EntryType : std::uint8_t {
    FindService = 0x00,
    OfferService = 0x01,
    SubscribeEventgroup = 0x06,
    SubscribeEventgroupAck = 0x07,
};

/// The value that goes in the option's L4-Proto byte.
enum class Layer4 : std::uint8_t {
    Tcp = 0x06,
    Udp = 0x11,
};

/// 0x04 is a unicast endpoint, 0x14 a multicast group. The two are different
/// option *types*, not a flag, which is how one offer says "call me here, and
/// listen for my events over there" in a single entry.
enum class OptionType : std::uint8_t {
    Ipv4Endpoint = 0x04,
    Ipv4Multicast = 0x14,
};

/// An IPv4 endpoint option.
///
/// It duplicates av::eth::Endpoint's address+port on purpose. Linking av_eth
/// into av_service would say the wire format depends on a socket
/// implementation, and it does not -- and this carries a transport-protocol
/// byte that a socket endpoint has no reason to hold.
struct Option {
    OptionType type{OptionType::Ipv4Endpoint};
    std::string address;
    Layer4 protocol{Layer4::Udp};
    std::uint16_t port{0};
};

struct Entry {
    EntryType type{EntryType::FindService};
    std::uint16_t service_id{0};
    /// Which *copy* of the service this is. Two identical radar ECUs offer the
    /// same service id with instance 1 and 2. 0xFFFF in a FindService means
    /// "any instance".
    std::uint16_t instance_id{1};
    std::uint8_t major_version{1};
    /// Seconds the offer stays valid. 0 = StopOffer, kTtlForever = no renewal.
    std::uint32_t ttl{0};
    /// Service entries only.
    std::uint32_t minor_version{0};
    /// Eventgroup entries only.
    std::uint16_t eventgroup_id{0};
    /// Endpoints this entry advertises. Empty for a FindService, which asks a
    /// question and offers nothing.
    std::vector<Option> options;
};

struct Message {
    /// Set until this sender's session id has wrapped once. It tells a receiver
    /// "I restarted" -- so a client can discard state from before the reboot
    /// instead of trusting session ids that have gone backwards.
    bool reboot{true};
    /// Set when the sender can also be reached by unicast SD, which is what
    /// lets a server answer a FindService directly instead of multicasting an
    /// offer at everyone.
    bool unicast{true};
    std::vector<Entry> entries;
};

/// True when a parsed SOME/IP message is an SD message.
[[nodiscard]] bool is_sd(const service::Header& header) noexcept;

/// Builds a complete datagram: SOME/IP header plus SD payload.
bool serialize(const Message& message, std::uint16_t session_id, std::vector<std::uint8_t>& out);

/// Parses the payload of a SOME/IP message already identified as SD.
///
/// Returns nullopt for a truncated payload, an array length that disagrees with
/// the bytes present, or an option whose declared length is not the 9 an IPv4
/// option must declare.
[[nodiscard]] std::optional<Message> parse_payload(const std::uint8_t* data, std::size_t size);

/// What changed about a service, from a client's point of view.
enum class Availability : std::uint8_t {
    Unchanged,    ///< a renewal of a lease that was already held
    Available,    ///< it was not there, and now it is
    Unavailable,  ///< the lease expired, or the server withdrew it
};

/// Tracks which services are currently offered, and for how much longer.
///
/// This is the state machine SD exists to drive. It is deliberately separate
/// from the wire format: the bytes are one problem, and "is the cluster allowed
/// to call GetSpeed right now" is another.
class ServiceRegistry {
public:
    using Clock = std::chrono::steady_clock;

    struct Change {
        std::uint16_t service_id{0};
        std::uint16_t instance_id{0};
        Availability availability{Availability::Unchanged};
    };

    /// Applies one OfferService entry. TTL 0 withdraws the service.
    ///
    /// Entries that are not offers are ignored rather than rejected: an SD
    /// message legitimately carries a mix, and a client that cannot handle
    /// eventgroup entries should still be able to read the offers beside them.
    Availability observe(const Entry& entry, Clock::time_point now);

    /// Drops every lease that has run out. This is where a dead server is
    /// noticed -- by nothing arriving, which no single message can express.
    std::vector<Change> expire(Clock::time_point now);

    [[nodiscard]] bool available(std::uint16_t service_id, std::uint16_t instance_id) const;

    /// The unicast endpoint to send requests to, if the service is offered.
    [[nodiscard]] std::optional<Option> method_endpoint(std::uint16_t service_id,
                                                        std::uint16_t instance_id) const;

    /// The multicast group its events are published to, if it advertised one.
    [[nodiscard]] std::optional<Option> event_endpoint(std::uint16_t service_id,
                                                       std::uint16_t instance_id) const;

    [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }

private:
    struct Record {
        std::optional<Option> method;
        std::optional<Option> events;
        Clock::time_point expires_at;
        bool forever{false};
    };

    /// service id in the high half, instance id in the low half. A service is
    /// only identified by both: one id, several ECUs.
    static std::uint32_t key_of(std::uint16_t service_id, std::uint16_t instance_id) noexcept {
        return (static_cast<std::uint32_t>(service_id) << 16U) | instance_id;
    }

    [[nodiscard]] const Record* find(std::uint16_t service_id, std::uint16_t instance_id) const;

    std::map<std::uint32_t, Record> records_;
};

}  // namespace av::service::sd
