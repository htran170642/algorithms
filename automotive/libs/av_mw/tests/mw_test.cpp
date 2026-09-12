// av_mw, end to end: two runtimes, real sockets, one thread.
//
// These are integration tests, not unit tests -- they open UDP sockets and join
// multicast groups on loopback -- and they stay deterministic for the reason
// the week 10 design gives: the runtime owns no thread. The test *is* the
// event loop. It polls the server's runtime and then the client's, in turn, so
// nothing happens that the test did not ask for.
//
// Each case pins one clause of the contract written at the top of
// av/mw/proxy.hpp. Between them they cover every way a call can end.

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "av/mw/proxy.hpp"
#include "av/mw/result.hpp"
#include "av/mw/runtime.hpp"
#include "av/mw/skeleton.hpp"
#include "av/service/sd.hpp"
#include "av/service/someip.hpp"

namespace {

namespace mw = av::mw;
namespace sd = av::service::sd;

using av::service::ReturnCode;
using std::chrono::milliseconds;

// Not the demo's numbers, so an svc_server left running in another terminal
// cannot answer a test's FindService and make it pass for the wrong reason.
constexpr const char* kSdGroup = "239.10.0.19";
constexpr std::uint16_t kSdPort = 31490;
constexpr const char* kEventGroup = "239.10.0.12";
constexpr std::uint16_t kMethodPort = 31509;
constexpr std::uint16_t kEventPort = 31510;

constexpr std::uint16_t kService = 0x4321;
constexpr std::uint16_t kInstance = 0x0001;
constexpr std::uint16_t kEcho = 0x0001;
constexpr std::uint16_t kUnimplemented = 0x0009;
constexpr std::uint16_t kTick = 0x8001;

mw::RuntimeConfig runtime_config() {
    mw::RuntimeConfig config;
    config.sd_group = kSdGroup;
    config.sd_port = kSdPort;
    config.find_interval = milliseconds{50};
    config.offer_interval = milliseconds{200};
    return config;
}

mw::SkeletonConfig skeleton_config(std::uint8_t interface_version) {
    mw::SkeletonConfig config;
    config.service_id = kService;
    config.instance_id = kInstance;
    config.interface_version = interface_version;
    config.advertised_address = "127.0.0.1";
    config.method_port = kMethodPort;
    sd::Option events;
    events.type = sd::OptionType::Ipv4Multicast;
    events.address = kEventGroup;
    events.port = kEventPort;
    config.events = events;
    return config;
}

mw::ProxyConfig proxy_config(milliseconds timeout = milliseconds{300},
                             std::size_t max_pending = 16) {
    mw::ProxyConfig config;
    config.service_id = kService;
    config.instance_id = kInstance;
    config.interface_version = 1;
    config.client_id = 0x0B01;
    config.timeout = timeout;
    config.max_pending = max_pending;
    return config;
}

mw::Reply echo(const mw::Payload& request) { return mw::Reply{ReturnCode::Ok, request}; }

/// Drives both runtimes on this thread until `done()` holds or `limit` passes.
template <typename Done>
bool pump(mw::Runtime& first, mw::Runtime& second, Done done,
          milliseconds limit = milliseconds{3000}) {
    const auto deadline = mw::Clock::now() + limit;
    while (mw::Clock::now() < deadline) {
        first.poll(milliseconds{5});
        second.poll(milliseconds{5});
        if (done()) {
            return true;
        }
    }
    return false;
}

/// Only one side runs. To the other side's peers it is, as far as the network
/// can tell, wedged.
template <typename Done>
bool pump_one(mw::Runtime& runtime, Done done, milliseconds limit = milliseconds{3000}) {
    const auto deadline = mw::Clock::now() + limit;
    while (mw::Clock::now() < deadline) {
        runtime.poll(milliseconds{5});
        if (done()) {
            return true;
        }
    }
    return false;
}

/// Keeps both sides running for a while, giving anything that should *not*
/// happen every chance to happen.
void idle(mw::Runtime& first, mw::Runtime& second, milliseconds how_long) {
    static_cast<void>(pump(first, second, [] { return false; }, how_long));
}

class Middleware : public ::testing::Test {
protected:
    void SetUp() override {
        server_runtime = mw::Runtime::create(runtime_config());
        client_runtime = mw::Runtime::create(runtime_config());
        ASSERT_NE(server_runtime, nullptr);
        ASSERT_NE(client_runtime, nullptr);
    }

    /// An offering server and a proxy that has found it.
    void connect(std::uint8_t interface_version = 1, mw::ProxyConfig config = proxy_config()) {
        skeleton = mw::Skeleton::create(*server_runtime, skeleton_config(interface_version));
        ASSERT_TRUE(skeleton.has_value());
        skeleton->on_method(kEcho, echo);
        proxy = mw::Proxy::create(*client_runtime, std::move(config));
        ASSERT_TRUE(proxy.has_value());
        skeleton->offer();
        ASSERT_TRUE(pump(*server_runtime, *client_runtime, [this] { return proxy->available(); }));
    }

    // Runtimes first, so they are destroyed last: the ownership rule from
    // runtime.hpp, obeyed here exactly as an application has to obey it.
    std::unique_ptr<mw::Runtime> server_runtime;
    std::unique_ptr<mw::Runtime> client_runtime;
    std::optional<mw::Skeleton> skeleton;
    std::optional<mw::Proxy> proxy;
};

}  // namespace

TEST_F(Middleware, FindsTheServiceThenCallsItExactlyOnce) {
    ASSERT_NO_FATAL_FAILURE(connect());

    int completions = 0;
    std::optional<mw::Payload> answer;
    proxy->call(kEcho, mw::Payload{1, 2, 3}, [&](const mw::Result<mw::Payload>& result) {
        ++completions;
        if (result.ok()) {
            answer = result.value();
        }
    });

    ASSERT_TRUE(pump(*server_runtime, *client_runtime, [&] { return completions > 0; }));
    idle(*server_runtime, *client_runtime, milliseconds{100});

    EXPECT_EQ(completions, 1);
    ASSERT_TRUE(answer.has_value());
    EXPECT_EQ(*answer, (mw::Payload{1, 2, 3}));
    EXPECT_EQ(proxy->stats().answered, 1U);
}

TEST_F(Middleware, ACallBeforeDiscoveryFailsLocallyAndNeverInsideCall) {
    auto lonely = mw::Proxy::create(*client_runtime, proxy_config());
    ASSERT_TRUE(lonely.has_value());

    bool completed = false;
    std::optional<mw::Failure> failure;
    lonely->call(kEcho, {}, [&](const mw::Result<mw::Payload>& result) {
        completed = true;
        if (!result.ok()) {
            failure = result.error().failure;
        }
    });

    // The rule this pins: code written *after* call() -- `waiting = true;` --
    // must run before the callback, even when the answer is known at once.
    EXPECT_FALSE(completed);

    client_runtime->poll(milliseconds{0});
    EXPECT_TRUE(completed);
    ASSERT_TRUE(failure.has_value());
    EXPECT_EQ(*failure, mw::Failure::NotAvailable);
    EXPECT_EQ(lonely->stats().not_available, 1U);
}

TEST_F(Middleware, AnUnimplementedMethodIsARemoteErrorNotATimeout) {
    ASSERT_NO_FATAL_FAILURE(connect());

    std::optional<mw::CallError> error;
    proxy->call(kUnimplemented, {}, [&](const mw::Result<mw::Payload>& result) {
        if (!result.ok()) {
            error = result.error();
        }
    });

    ASSERT_TRUE(pump(*server_runtime, *client_runtime, [&] { return error.has_value(); }));
    EXPECT_EQ(error->failure, mw::Failure::Remote);
    EXPECT_EQ(error->code, ReturnCode::UnknownMethod);
}

TEST_F(Middleware, TheServerRefusesAnInterfaceVersionItDoesNotSpeak) {
    ASSERT_NO_FATAL_FAILURE(connect(2));

    std::optional<mw::CallError> error;
    proxy->call(kEcho, {}, [&](const mw::Result<mw::Payload>& result) {
        if (!result.ok()) {
            error = result.error();
        }
    });

    ASSERT_TRUE(pump(*server_runtime, *client_runtime, [&] { return error.has_value(); }));
    EXPECT_EQ(error->failure, mw::Failure::Remote);
    EXPECT_EQ(error->code, ReturnCode::WrongInterfaceVersion);
}

TEST_F(Middleware, AnUnansweredCallTimesOut) {
    ASSERT_NO_FATAL_FAILURE(connect(1, proxy_config(milliseconds{50})));

    std::optional<mw::Failure> failure;
    proxy->call(kEcho, {}, [&](const mw::Result<mw::Payload>& result) {
        if (!result.ok()) {
            failure = result.error().failure;
        }
    });

    // The request sits unread in the server's socket: from outside, exactly
    // what a wedged server looks like.
    ASSERT_TRUE(pump_one(*client_runtime, [&] { return failure.has_value(); }));
    EXPECT_EQ(*failure, mw::Failure::Timeout);
    EXPECT_EQ(proxy->stats().timeouts, 1U);
}

TEST_F(Middleware, BeyondMaxPendingACallIsRefusedNotQueued) {
    ASSERT_NO_FATAL_FAILURE(connect(1, proxy_config(milliseconds{5000}, 2)));

    int answered = 0;
    std::vector<mw::Failure> failures;
    const auto record = [&](const mw::Result<mw::Payload>& result) {
        if (result.ok()) {
            ++answered;
        } else {
            failures.push_back(result.error().failure);
        }
    };
    proxy->call(kEcho, {}, record);
    proxy->call(kEcho, {}, record);
    proxy->call(kEcho, {}, record);  // the third: no room

    client_runtime->poll(milliseconds{0});
    ASSERT_EQ(failures.size(), 1U);
    EXPECT_EQ(failures.front(), mw::Failure::Busy);
    EXPECT_EQ(proxy->stats().busy, 1U);

    // The two that were accepted are still answered normally.
    ASSERT_TRUE(pump(*server_runtime, *client_runtime, [&] { return answered == 2; }));
}

TEST_F(Middleware, AStopOfferFailsWaitingCallsAtOnceInsteadOfAtTheirTimeout) {
    ASSERT_NO_FATAL_FAILURE(connect(1, proxy_config(milliseconds{5000})));

    bool became_unavailable = false;
    proxy->on_availability([&](bool available) {
        if (!available) {
            became_unavailable = true;
        }
    });

    std::optional<mw::Failure> failure;
    proxy->call(kEcho, {}, [&](const mw::Result<mw::Payload>& result) {
        if (!result.ok()) {
            failure = result.error().failure;
        }
    });

    const auto start = mw::Clock::now();
    skeleton.reset();  // the destructor sends the StopOffer

    ASSERT_TRUE(pump_one(*client_runtime, [&] { return failure.has_value(); }));
    EXPECT_EQ(*failure, mw::Failure::NotAvailable);
    EXPECT_LT(mw::Clock::now() - start, milliseconds{1000})
        << "the call sat out its 5 s timeout instead of failing with the service";
    EXPECT_TRUE(became_unavailable);
    EXPECT_FALSE(proxy->available());
}

TEST_F(Middleware, EventsReachASubscriberWithoutAnyCall) {
    ASSERT_NO_FATAL_FAILURE(connect());

    std::optional<mw::Payload> received;
    proxy->subscribe(kTick, [&](const mw::Payload& payload) { received = payload; });

    // An event is a sample. One sent before the membership exists is simply
    // not received, so keep publishing until one lands.
    ASSERT_TRUE(pump(*server_runtime, *client_runtime, [&] {
        skeleton->notify(kTick, mw::Payload{42});
        return received.has_value();
    }));
    EXPECT_EQ(*received, mw::Payload{42});
    EXPECT_GE(proxy->stats().events, 1U);
}

TEST_F(Middleware, AProxyMayBeDestroyedFromInsideItsOwnCallback) {
    ASSERT_NO_FATAL_FAILURE(connect());

    int completions = 0;
    proxy->call(kEcho, {}, [&](const mw::Result<mw::Payload>& /*result*/) {
        ++completions;
        proxy.reset();  // the owner goes away in the middle of its own dispatch
    });

    ASSERT_TRUE(pump(*server_runtime, *client_runtime, [&] { return completions > 0; }));
    idle(*server_runtime, *client_runtime, milliseconds{100});

    // Under ASan, any touch of the proxy's freed state after the callback
    // returned would have failed this test before reaching here.
    EXPECT_EQ(completions, 1);
    EXPECT_FALSE(proxy.has_value());
}

TEST_F(Middleware, ALateListenerIsToldTheCurrentStateOnTheNextPoll) {
    ASSERT_NO_FATAL_FAILURE(connect());

    std::optional<bool> heard;
    proxy->on_availability([&](bool available) { heard = available; });
    EXPECT_FALSE(heard.has_value());

    client_runtime->poll(milliseconds{0});
    ASSERT_TRUE(heard.has_value());
    EXPECT_TRUE(*heard);
}
