#include <gtest/gtest.h>

#include <stdexcept>
#include <utility>

#include "vector.hpp"

using cr::Vector;

// ================================================= 1. geometric growth
TEST(W5Vector, ThousandPushesRealllocAboutTenTimes) {
    Vector<int>::resetStats();
    Vector<int> v;

    for (int i = 0; i < 1000; ++i) {
        v.push_back(i);
    }

    EXPECT_EQ(v.size(), 1000u);
    EXPECT_LE(Vector<int>::reallocs, 11);     // ~log2(1000), NOT 1000
    EXPECT_GE(Vector<int>::reallocs, 10);
    EXPECT_EQ(v[0], 0);
    EXPECT_EQ(v[999], 999);
}

TEST(W5Vector, CapacityDoublesSizeGrowsByOne) {
    Vector<int> v;
    EXPECT_EQ(v.capacity(), 0u);

    v.push_back(1);
    EXPECT_EQ(v.capacity(), 1u);
    v.push_back(2);
    EXPECT_EQ(v.capacity(), 2u);
    v.push_back(3);
    EXPECT_EQ(v.capacity(), 4u);      // doubled from 2, not grown to 3
    v.push_back(4);
    EXPECT_EQ(v.capacity(), 4u);
    v.push_back(5);
    EXPECT_EQ(v.capacity(), 8u);
}

// ============================ 2. move_if_noexcept: noexcept => move, else copy
namespace {

struct NoexceptMove {
    int v = 0;
    NoexceptMove() = default;
    NoexceptMove(int x) : v(x) {}
    NoexceptMove(const NoexceptMove& o) : v(o.v) { ++copies; }
    NoexceptMove(NoexceptMove&& o) noexcept : v(o.v) { ++moves; }   // noexcept
    static inline int copies = 0, moves = 0;
    static void reset() { copies = moves = 0; }
};

struct ThrowingMove {
    int v = 0;
    ThrowingMove() = default;
    ThrowingMove(int x) : v(x) {}
    ThrowingMove(const ThrowingMove& o) : v(o.v) { ++copies; }
    ThrowingMove(ThrowingMove&& o) : v(o.v) { ++moves; }           // NOT noexcept
    static inline int copies = 0, moves = 0;
    static void reset() { copies = moves = 0; }
};

}  // namespace

TEST(W5Vector, NoexceptMoveIsMovedOnRealloc) {
    NoexceptMove::reset();
    Vector<NoexceptMove> v;
    v.push_back(NoexceptMove{1});
    v.push_back(NoexceptMove{2});     // forces a realloc of the existing element

    EXPECT_GT(NoexceptMove::moves, 0);
    EXPECT_EQ(NoexceptMove::copies, 0);   // the existing element was MOVED
}

TEST(W5Vector, ThrowingMoveIsCopiedOnRealloc) {
    ThrowingMove::reset();
    Vector<ThrowingMove> v;
    v.push_back(ThrowingMove{1});
    v.push_back(ThrowingMove{2});     // same realloc...

    EXPECT_GT(ThrowingMove::copies, 0);   // ...but this time it COPIED
    // vector refused to move because a throwing move can't keep the guarantee
}

// ============================ 3. strong guarantee: throw mid-realloc, data intact
namespace {

struct BombOnCopy {
    int v = 0;
    static inline int live = 0;
    static inline int throw_after = -1;
    static inline int copies_done = 0;

    BombOnCopy(int x = 0) : v(x) { ++live; }
    BombOnCopy(const BombOnCopy& o) : v(o.v) {
        if (throw_after >= 0 && copies_done >= throw_after) {
            throw std::runtime_error("copy bomb");
        }
        ++copies_done;
        ++live;
    }
    // Deliberately non-noexcept move so realloc chooses the copy path.
    BombOnCopy(BombOnCopy&& o) : v(o.v) { ++live; }
    ~BombOnCopy() { --live; }

    static void reset() { live = 0; throw_after = -1; copies_done = 0; }
};

}  // namespace

TEST(W5Vector, StrongGuaranteeNoLeakWhenCopyThrowsMidRealloc) {
    BombOnCopy::reset();
    Vector<BombOnCopy> v;
    v.reserve(2);
    v.emplace_back(1);
    v.emplace_back(2);

    const int live_before = BombOnCopy::live;

    // Next realloc copies 2 elements; blow up on the 2nd copy.
    BombOnCopy::throw_after = 1;

    EXPECT_THROW(v.reserve(4), std::runtime_error);

    // No leak: the partially-built new buffer was cleaned up.
    EXPECT_EQ(BombOnCopy::live, live_before);
    // Old buffer intact: the vector is still usable and unchanged.
    EXPECT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0].v, 1);
    EXPECT_EQ(v[1].v, 2);
}
