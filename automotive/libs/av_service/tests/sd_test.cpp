// Service Discovery: the bytes, and the lease.
//
// Two things are worth testing here and they are not the same thing.
//
//   * The **wire format**, pinned byte by byte for the same reason week 8's
//     header was: an encoder and a decoder that are wrong together round-trip
//     perfectly. SD has its own version of the Length trap -- an option
//     declares 9 and occupies 12 -- so the bytes are asserted, not the round
//     trip.
//   * The **TTL state machine**, which is where "the server died" becomes an
//     observable event. It is driven by a clock, so the tests inject the time
//     rather than sleeping: a test that waits three seconds to watch a lease
//     expire is a test nobody runs.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <vector>

#include "av/service/sd.hpp"
#include "av/service/someip.hpp"

namespace {

namespace sd = av::service::sd;

using Clock = sd::ServiceRegistry::Clock;

sd::Option method_option() {
    sd::Option option;
    option.type = sd::OptionType::Ipv4Endpoint;
    option.address = "127.0.0.1";
    option.protocol = sd::Layer4::Udp;
    option.port = 30509;
    return option;
}

sd::Option event_option() {
    sd::Option option;
    option.type = sd::OptionType::Ipv4Multicast;
    option.address = "239.10.0.2";
    option.protocol = sd::Layer4::Udp;
    option.port = 30510;
    return option;
}

sd::Entry offer(std::uint32_t ttl) {
    sd::Entry entry;
    entry.type = sd::EntryType::OfferService;
    entry.service_id = 0x1234;
    entry.instance_id = 0x0001;
    entry.major_version = 1;
    entry.ttl = ttl;
    entry.minor_version = 0;
    entry.options = {method_option(), event_option()};
    return entry;
}

sd::Message offer_message(std::uint32_t ttl) {
    sd::Message message;
    message.reboot = true;
    message.unicast = true;
    message.entries = {offer(ttl)};
    return message;
}

}  // namespace

TEST(ServiceDiscoveryWire, EncodesAnOfferByteForByte) {
    std::vector<std::uint8_t> wire;
    ASSERT_TRUE(sd::serialize(offer_message(3), 0x0001, wire));

    const std::vector<std::uint8_t> expected{
        // ---- SOME/IP header: SD is not special, it is a Notification -------
        0xFF, 0xFF,              // service id 0xFFFF -- SD's own "service"
        0x81, 0x00,              // method id 0x8100
        0x00, 0x00, 0x00, 0x3C,  // length = 8 + 52 payload
        0x00, 0x00,              // client id 0 -- SD answers nobody
        0x00, 0x01,              // session id
        0x01, 0x01, 0x02, 0x00,  // proto, iface, Notification, E_OK

        // ---- SD payload ----------------------------------------------------
        0xC0,                    // flags: reboot | unicast
        0x00, 0x00, 0x00,        // reserved
        0x00, 0x00, 0x00, 0x10,  // entries array = 16 bytes = one entry

        // ---- entry ---------------------------------------------------------
        0x01,                    // OfferService
        0x00,                    // index of the first option run
        0x00,                    // index of the second run -- unused
        0x20,                    // 2 options in the first run, 0 in the second
        0x12, 0x34,              // service id
        0x00, 0x01,              // instance id
        0x01,                    // major version
        0x00, 0x00, 0x03,        // TTL = 3 s  <- 24 bits, not 32
        0x00, 0x00, 0x00, 0x00,  // minor version

        0x00, 0x00, 0x00, 0x18,  // options array = 24 bytes = two options

        // ---- option 0: where to send requests -------------------------------
        0x00, 0x09,              // length 9  <- the option occupies 12
        0x04,                    // IPv4 unicast endpoint
        0x00,                    // reserved
        0x7F, 0x00, 0x00, 0x01,  // 127.0.0.1
        0x00,                    // reserved
        0x11,                    // L4 = UDP
        0x77, 0x2D,              // port 30509

        // ---- option 1: where the events are published -----------------------
        0x00, 0x09,              // length 9 again
        0x14,                    // IPv4 *multicast* -- a different option type
        0x00,                    // reserved
        0xEF, 0x0A, 0x00, 0x02,  // 239.10.0.2
        0x00,                    // reserved
        0x11,                    // L4 = UDP
        0x77, 0x2E,              // port 30510
    };

    EXPECT_EQ(wire, expected);
}

TEST(ServiceDiscoveryWire, RoundTripsAnOfferIncludingItsEndpoints) {
    std::vector<std::uint8_t> wire;
    ASSERT_TRUE(sd::serialize(offer_message(3), 0x0001, wire));

    const auto message = av::service::deserialize(wire.data(), wire.size());
    ASSERT_TRUE(message.has_value());
    EXPECT_TRUE(sd::is_sd(message->header));

    const auto parsed = sd::parse_payload(message->payload.data(), message->payload.size());
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->reboot);
    EXPECT_TRUE(parsed->unicast);
    ASSERT_EQ(parsed->entries.size(), 1U);

    const auto& entry = parsed->entries.front();
    EXPECT_EQ(entry.type, sd::EntryType::OfferService);
    EXPECT_EQ(entry.service_id, 0x1234);
    EXPECT_EQ(entry.instance_id, 0x0001);
    EXPECT_EQ(entry.ttl, 3U);
    ASSERT_EQ(entry.options.size(), 2U);
    EXPECT_EQ(entry.options[0].type, sd::OptionType::Ipv4Endpoint);
    EXPECT_EQ(entry.options[0].address, "127.0.0.1");
    EXPECT_EQ(entry.options[0].port, 30509);
    EXPECT_EQ(entry.options[1].type, sd::OptionType::Ipv4Multicast);
    EXPECT_EQ(entry.options[1].address, "239.10.0.2");
    EXPECT_EQ(entry.options[1].port, 30510);
}

TEST(ServiceDiscoveryWire, FindServiceCarriesNoEndpoint) {
    sd::Entry find;
    find.type = sd::EntryType::FindService;
    find.service_id = 0x1234;
    find.instance_id = 0xFFFF;  // any instance
    find.ttl = 3;

    sd::Message message;
    message.entries = {find};

    std::vector<std::uint8_t> wire;
    ASSERT_TRUE(sd::serialize(message, 0x0001, wire));

    // A question offers nothing, so the options array is empty and the whole
    // message is header + flags + two array lengths + one entry.
    EXPECT_EQ(wire.size(), av::service::kHeaderSize + 12 + sd::kEntrySize);

    const auto parsed_message = av::service::deserialize(wire.data(), wire.size());
    ASSERT_TRUE(parsed_message.has_value());
    const auto parsed =
        sd::parse_payload(parsed_message->payload.data(), parsed_message->payload.size());
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->entries.size(), 1U);
    EXPECT_EQ(parsed->entries[0].type, sd::EntryType::FindService);
    EXPECT_EQ(parsed->entries[0].instance_id, 0xFFFF);
    EXPECT_TRUE(parsed->entries[0].options.empty());
}

TEST(ServiceDiscoveryWire, RefusesAnOptionArrayThatDisagreesWithThePayload) {
    std::vector<std::uint8_t> wire;
    ASSERT_TRUE(sd::serialize(offer_message(3), 0x0001, wire));

    const auto message = av::service::deserialize(wire.data(), wire.size());
    ASSERT_TRUE(message.has_value());
    auto payload = message->payload;

    // Claim one more option byte than was sent. A peer that computes this
    // length over the wrong range produces exactly this, and the failure has
    // to surface here rather than three layers up as "the endpoint is garbage".
    payload[27] = 0x19;
    EXPECT_FALSE(sd::parse_payload(payload.data(), payload.size()).has_value());
}

TEST(ServiceRegistry, FirstOfferIsNewsAndRenewalsAreNot) {
    sd::ServiceRegistry registry;
    const auto t0 = Clock::now();

    EXPECT_EQ(registry.observe(offer(3), t0), sd::Availability::Available);
    EXPECT_EQ(registry.observe(offer(3), t0 + std::chrono::seconds{1}),
              sd::Availability::Unchanged);
    EXPECT_EQ(registry.size(), 1U);
}

TEST(ServiceRegistry, LearnsBothEndpointsFromOneOffer) {
    sd::ServiceRegistry registry;
    registry.observe(offer(3), Clock::now());

    const auto method = registry.method_endpoint(0x1234, 0x0001);
    ASSERT_TRUE(method.has_value());
    EXPECT_EQ(method->address, "127.0.0.1");
    EXPECT_EQ(method->port, 30509);

    // This is the constant that vehicle_service.hpp used to hold.
    const auto events = registry.event_endpoint(0x1234, 0x0001);
    ASSERT_TRUE(events.has_value());
    EXPECT_EQ(events->address, "239.10.0.2");
    EXPECT_EQ(events->port, 30510);
}

TEST(ServiceRegistry, ALeaseThatIsNotRenewedExpires) {
    sd::ServiceRegistry registry;
    const auto t0 = Clock::now();
    registry.observe(offer(3), t0);

    // Still inside the lease: nothing has happened, and nothing should be
    // reported. Silence is not yet evidence.
    EXPECT_TRUE(registry.expire(t0 + std::chrono::seconds{2}).empty());
    EXPECT_TRUE(registry.available(0x1234, 0x0001));

    // Past it. No message said the server died -- the absence of one did.
    const auto changes = registry.expire(t0 + std::chrono::seconds{4});
    ASSERT_EQ(changes.size(), 1U);
    EXPECT_EQ(changes[0].service_id, 0x1234);
    EXPECT_EQ(changes[0].availability, sd::Availability::Unavailable);
    EXPECT_FALSE(registry.available(0x1234, 0x0001));
    EXPECT_FALSE(registry.method_endpoint(0x1234, 0x0001).has_value());
}

TEST(ServiceRegistry, StopOfferWithdrawsImmediately) {
    sd::ServiceRegistry registry;
    const auto t0 = Clock::now();
    ASSERT_EQ(registry.observe(offer(3), t0), sd::Availability::Available);

    // TTL 0 is the polite shutdown: clients react now instead of waiting out
    // the remaining lease.
    EXPECT_EQ(registry.observe(offer(sd::kTtlStop), t0 + std::chrono::milliseconds{10}),
              sd::Availability::Unavailable);
    EXPECT_FALSE(registry.available(0x1234, 0x0001));

    // A StopOffer for something never offered is not an event.
    EXPECT_EQ(registry.observe(offer(sd::kTtlStop), t0), sd::Availability::Unchanged);
}

TEST(ServiceRegistry, TwoInstancesOfOneServiceAreDifferentServices) {
    sd::ServiceRegistry registry;
    const auto t0 = Clock::now();

    sd::Entry second = offer(3);
    second.instance_id = 0x0002;
    second.options[0].port = 30519;

    EXPECT_EQ(registry.observe(offer(3), t0), sd::Availability::Available);
    EXPECT_EQ(registry.observe(second, t0), sd::Availability::Available);
    EXPECT_EQ(registry.size(), 2U);
    EXPECT_EQ(registry.method_endpoint(0x1234, 0x0001)->port, 30509);
    EXPECT_EQ(registry.method_endpoint(0x1234, 0x0002)->port, 30519);
}

TEST(ServiceRegistry, IgnoresEntriesThatAreNotOffers) {
    sd::ServiceRegistry registry;
    sd::Entry find;
    find.type = sd::EntryType::FindService;
    find.service_id = 0x1234;
    find.ttl = 3;

    // A FindService is a question, not an announcement. Treating it as an offer
    // would make every client believe a service exists because somebody asked
    // for it.
    EXPECT_EQ(registry.observe(find, Clock::now()), sd::Availability::Unchanged);
    EXPECT_EQ(registry.size(), 0U);
}
