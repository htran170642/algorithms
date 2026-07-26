// W27 — every layout claim, proven at compile time.
//
// Almost all of this is static_assert: layout is a compile-time property, so the
// right place to pin it is the compiler, not a runtime EXPECT. The single
// TEST body exists only so the file links into the gtest binary and the matrix
// (C++20 + C++23, debug/asan/ubsan) actually compiles these assertions.

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "w27/layout.hpp"

namespace {

// --- Law 1: internal padding — members land on their own alignment ----------
static_assert(alignof(double) == 8, "double is 8-aligned on this ABI");

static_assert(offsetof(w27::Bad, a) == 0);
static_assert(offsetof(w27::Bad, b) == 8);   // 7 bytes internal padding jumped a->b
static_assert(offsetof(w27::Bad, c) == 16);

// --- Law 2: trailing padding — sizeof rounds up to alignof(struct) -----------
static_assert(alignof(w27::Bad) == 8);       // inherited from the double
static_assert(sizeof(w27::Bad) == 24);       // 10 bytes of data, padded to 24
static_assert(sizeof(w27::Bad) % alignof(w27::Bad) == 0);  // the array-stride law

// --- Law 3: reorder kills INTERNAL padding, not TRAILING ---------------------
static_assert(offsetof(w27::Good, b) == 0);
static_assert(offsetof(w27::Good, a) == 8);
static_assert(offsetof(w27::Good, c) == 9);
static_assert(sizeof(w27::Good) == 16);      // 24 -> 16 by reordering...
static_assert(sizeof(w27::Good) != 10);      // ...but NOT down to 10: trailing pad remains
static_assert(alignof(w27::Good) == 8);

// The reorder is a pure win: same fields, smaller footprint, zero cost.
static_assert(sizeof(w27::Good) < sizeof(w27::Bad));

// --- The zero-padding specimen ----------------------------------------------
static_assert(sizeof(w27::Snug) == 16);
static_assert(offsetof(w27::Snug, x) == 0);
static_assert(offsetof(w27::Snug, y) == 8);
static_assert(offsetof(w27::Snug, z) == 12);
static_assert(offsetof(w27::Snug, w) == 14);
static_assert(offsetof(w27::Snug, v) == 15);
// Proof there is no padding: sizeof equals the exact sum of member sizes.
static_assert(sizeof(w27::Snug) == 8 + 4 + 2 + 1 + 1);

// --- alignas only WIDENS ------------------------------------------------------
static_assert(alignof(w27::CacheLinePadded) == 64);
static_assert(sizeof(w27::CacheLinePadded) == 64);   // one int64 blown up to a full line
static_assert(sizeof(w27::CacheLinePadded) > sizeof(std::int64_t));

// --- [[no_unique_address]] only SHRINKS --------------------------------------
static_assert(sizeof(w27::WithoutAttr) == 8);   // empty member costs 1 + 3 pad
static_assert(sizeof(w27::WithAttr) == 4);      // empty member overlaps the int
static_assert(sizeof(w27::WithAttr) < sizeof(w27::WithoutAttr));

TEST(Layout, EverythingProvenAtCompileTime) {
    // Runtime spot-check that offsetof/sizeof agree with an actual object's
    // address arithmetic — the static_asserts above already carry the weight.
    w27::Bad b{};
    const auto* base = reinterpret_cast<const std::byte*>(&b);
    const auto* pd   = reinterpret_cast<const std::byte*>(&b.b);
    EXPECT_EQ(pd - base, 8);
    SUCCEED();
}

}  // namespace
