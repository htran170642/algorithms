#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "forwarding.hpp"
#include "probe.hpp"

using cr::Deduced;
using cr::Probe;

// ================================ 1. reference collapsing: T carries the answer
TEST(W6Forward, TCarriesTheValueCategory) {
    Probe p{"x"};

    EXPECT_EQ(cr::whatWasDeduced(p), Deduced::Lvalue);              // T = Probe&
    EXPECT_EQ(cr::whatWasDeduced(Probe{"y"}), Deduced::Rvalue);     // T = Probe
    EXPECT_EQ(cr::whatWasDeduced(std::move(p)), Deduced::Rvalue);   // T = Probe
}

// ============================ 2. the point: forward preserves, nothing else does
namespace {

// The naive wrapper: takes by const&, so it can only ever copy.
template <typename T>
Probe wrapByConstRef(const T& x) { return Probe(x); }

// The broken wrapper: std::move on a forwarding reference steals unconditionally.
template <typename T>
Probe wrapByMove(T&& x) { return Probe(cr::move(x)); }

// The correct wrapper: preserves whatever it was handed.
template <typename T>
Probe wrapByForward(T&& x) { return Probe(cr::forward<T>(x)); }

}  // namespace

TEST(W6Forward, ConstRefAlwaysCopiesEvenForRvalues) {
    Probe::setVerbose(false);
    Probe::reset();

    Probe sink = wrapByConstRef(Probe{"tmp"});   // an rvalue -- should be movable

    EXPECT_EQ(Probe::counters().copy_ctor, 1);   // but const& forced a COPY
    EXPECT_EQ(Probe::counters().move_ctor, 0);
}

TEST(W6Forward, ForwardMovesRvalues) {
    Probe::setVerbose(false);
    Probe::reset();

    Probe sink = wrapByForward(Probe{"tmp"});    // rvalue in -> rvalue out

    EXPECT_EQ(Probe::counters().move_ctor, 1);   // MOVED
    EXPECT_EQ(Probe::counters().copy_ctor, 0);
}

TEST(W6Forward, ForwardCopiesLvalues) {
    Probe::setVerbose(false);
    Probe::reset();

    Probe original{"keep"};
    Probe sink = wrapByForward(original);        // lvalue in -> lvalue out

    EXPECT_EQ(Probe::counters().copy_ctor, 1);   // COPIED -- correct!
    EXPECT_EQ(Probe::counters().move_ctor, 0);
    EXPECT_EQ(original.name(), "keep");          // caller's object untouched
}

// ================== 3. THE BUG: std::move on a forwarding reference
TEST(W6Forward, MoveOnForwardingReferenceStealsTheCallersLvalue) {
    Probe::setVerbose(false);
    Probe::reset();

    Probe original{"precious"};
    Probe sink = wrapByMove(original);           // caller passed an LVALUE...

    EXPECT_EQ(Probe::counters().move_ctor, 1);   // ...and it was moved anyway
    EXPECT_EQ(original.name(), "<moved-from>");  // the caller's object was GUTTED
    // This is why std::move on T&& is a bug. forward is not optional.
}

// ================================================== 4. makeUnique
TEST(W6Forward, MakeUniqueForwardsEachArgIndependently) {
    Probe::setVerbose(false);
    Probe::reset();

    Probe lv{"lvalue"};
    auto up = cr::makeUnique<std::pair<Probe, Probe>>(lv, Probe{"rvalue"});

    EXPECT_EQ(Probe::counters().copy_ctor, 1);   // lv was copied
    EXPECT_GE(Probe::counters().move_ctor, 1);   // the temporary was moved
    EXPECT_EQ(lv.name(), "lvalue");              // still intact
}

TEST(W6Forward, MakeUniqueWorksWithZeroArgs) {
    auto up = cr::makeUnique<std::string>();
    EXPECT_TRUE(up->empty());
}
