#include "av/ipc/shared_ring.hpp"

#include <gtest/gtest.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <string>
#include <thread>

namespace {

using av::ipc::SharedRing;

/// A trivially copyable payload -- the only kind that can cross an address
/// space boundary by memcpy.
struct Sample {
    std::uint64_t sequence{};
    double speed{};
};

std::string unique_name(const char* tag) {
    return "/av-ring-" + std::to_string(::getpid()) + "-" + tag;
}

// --- Naming and lifetime ---------------------------------------------------

TEST(SharedRing, RejectsAnInvalidPosixName) {
    EXPECT_FALSE(SharedRing<Sample>::create("no-leading-slash", 4).has_value());
    EXPECT_FALSE(SharedRing<Sample>::create("/nested/name", 4).has_value());
    EXPECT_FALSE(SharedRing<Sample>::create("/", 4).has_value());
}

TEST(SharedRing, RejectsZeroCapacity) {
    EXPECT_FALSE(SharedRing<Sample>::create(unique_name("zero"), 0).has_value());
}

TEST(SharedRing, RefusesToCreateTheSameNameTwice) {
    const std::string name = unique_name("dup");
    auto first = SharedRing<Sample>::create(name, 4);
    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(SharedRing<Sample>::create(name, 4).has_value())
        << "O_EXCL: two creators would corrupt each other's indices";
}

TEST(SharedRing, OpeningAnAbsentNameFails) {
    EXPECT_FALSE(SharedRing<Sample>::open(unique_name("absent")).has_value());
}

TEST(SharedRing, CreatorUnlinksTheNameOnDestruction) {
    const std::string name = unique_name("unlink");
    {
        auto ring = SharedRing<Sample>::create(name, 4);
        ASSERT_TRUE(ring.has_value());
        EXPECT_TRUE(SharedRing<Sample>::open(name).has_value());
    }
    EXPECT_FALSE(SharedRing<Sample>::open(name).has_value());
}

TEST(SharedRing, RejectsAPeerWithADifferentElementSize) {
    struct Other {
        std::uint8_t byte{};
    };
    const std::string name = unique_name("mismatch");
    auto ring = SharedRing<Sample>::create(name, 4);
    ASSERT_TRUE(ring.has_value());

    EXPECT_FALSE(SharedRing<Other>::open(name).has_value())
        << "a peer that disagrees about T would read garbage from every slot";
}

// --- Ring behaviour --------------------------------------------------------

TEST(SharedRing, RoundTripsThroughSharedMemory) {
    const std::string name = unique_name("roundtrip");
    auto writer = SharedRing<Sample>::create(name, 4);
    ASSERT_TRUE(writer.has_value());
    auto reader = SharedRing<Sample>::open(name);
    ASSERT_TRUE(reader.has_value());

    ASSERT_TRUE(writer->try_push(Sample{1, 82.5}));

    Sample out{};
    ASSERT_TRUE(reader->try_pop(out));
    EXPECT_EQ(out.sequence, 1U);
    EXPECT_DOUBLE_EQ(out.speed, 82.5);
}

TEST(SharedRing, IsFirstInFirstOut) {
    const std::string name = unique_name("fifo");
    auto ring = SharedRing<Sample>::create(name, 8);
    ASSERT_TRUE(ring.has_value());

    for (std::uint64_t i = 0; i < 8U; ++i) {
        ASSERT_TRUE(ring->try_push(Sample{i, 0.0}));
    }
    for (std::uint64_t i = 0; i < 8U; ++i) {
        Sample out{};
        ASSERT_TRUE(ring->try_pop(out));
        EXPECT_EQ(out.sequence, i);
    }
}

TEST(SharedRing, ReportsFullInsteadOfOverwriting) {
    const std::string name = unique_name("full");
    auto ring = SharedRing<Sample>::create(name, 2);
    ASSERT_TRUE(ring.has_value());

    EXPECT_TRUE(ring->try_push(Sample{1, 0.0}));
    EXPECT_TRUE(ring->try_push(Sample{2, 0.0}));
    EXPECT_FALSE(ring->try_push(Sample{3, 0.0}));
    EXPECT_EQ(ring->size(), 2U);

    Sample out{};
    EXPECT_TRUE(ring->try_pop(out));
    EXPECT_EQ(out.sequence, 1U) << "the oldest survives; the newest was refused";
    EXPECT_TRUE(ring->try_push(Sample{3, 0.0}));
}

TEST(SharedRing, ReportsEmpty) {
    const std::string name = unique_name("empty");
    auto ring = SharedRing<Sample>::create(name, 2);
    ASSERT_TRUE(ring.has_value());

    Sample out{};
    EXPECT_FALSE(ring->try_pop(out));
}

TEST(SharedRing, IndicesWrapWithoutLosingOrder) {
    // Monotonic counters modulo capacity: push and pop far more than capacity
    // so tail % capacity wraps many times.
    const std::string name = unique_name("wrap");
    auto ring = SharedRing<Sample>::create(name, 3);
    ASSERT_TRUE(ring.has_value());

    for (std::uint64_t i = 0; i < 100U; ++i) {
        ASSERT_TRUE(ring->try_push(Sample{i, 0.0})) << "i=" << i;
        Sample out{};
        ASSERT_TRUE(ring->try_pop(out));
        EXPECT_EQ(out.sequence, i);
    }
}

// --- Concurrency (TSan validates the acquire/release pairing) --------------

TEST(SharedRing, SingleProducerSingleConsumerLosesNothing) {
    constexpr std::uint64_t kCount = 20000;
    const std::string name = unique_name("spsc");

    auto writer = SharedRing<Sample>::create(name, 64);
    ASSERT_TRUE(writer.has_value());
    auto reader = SharedRing<Sample>::open(name);
    ASSERT_TRUE(reader.has_value());

    std::uint64_t received = 0;

    std::thread consumer([&] {
        Sample out{};
        while (received < kCount) {
            if (reader->try_pop(out)) {
                // Order must be exact: a missing release store would show up
                // here as a gap or a repeat, not merely as a wrong count.
                EXPECT_EQ(out.sequence, received);
                ++received;
            }
        }
    });

    for (std::uint64_t i = 0; i < kCount; ++i) {
        while (!writer->try_push(Sample{i, static_cast<double>(i)})) {
            // Ring full: spin. A real producer would back off or drop; this is
            // a test of correctness, not of policy.
        }
    }
    consumer.join();

    EXPECT_EQ(received, kCount);
}

// --- Across a real process boundary ----------------------------------------

TEST(SharedRing, WorksBetweenTwoProcesses) {
    constexpr std::uint64_t kCount = 1000;
    const std::string name = unique_name("fork");

    auto writer = SharedRing<Sample>::create(name, 32);
    ASSERT_TRUE(writer.has_value());

    const pid_t child = ::fork();
    ASSERT_GE(child, 0);

    if (child == 0) {
        // Child: a genuinely separate address space. The ring works only
        // because both processes mapped the same pages.
        auto reader = SharedRing<Sample>::open(name);
        if (!reader) {
            ::_exit(1);
        }
        Sample out{};
        std::uint64_t seen = 0;
        while (seen < kCount) {
            if (reader->try_pop(out)) {
                if (out.sequence != seen) {
                    ::_exit(2);
                }
                ++seen;
            }
        }
        ::_exit(0);
    }

    for (std::uint64_t i = 0; i < kCount; ++i) {
        while (!writer->try_push(Sample{i, 0.0})) {
        }
    }

    int status = 0;
    ASSERT_EQ(::waitpid(child, &status, 0), child);
    // glibc defines these macros in a private header pulled in by <sys/wait.h>,
    // which include-cleaner cannot name, so it asks for an include that does
    // not exist. <sys/wait.h> is already the right one.
    // NOLINTNEXTLINE(misc-include-cleaner)
    ASSERT_TRUE(WIFEXITED(status));
    // NOLINTNEXTLINE(misc-include-cleaner)
    EXPECT_EQ(WEXITSTATUS(status), 0) << "1 = child could not open, 2 = out-of-order read";
}

}  // namespace
