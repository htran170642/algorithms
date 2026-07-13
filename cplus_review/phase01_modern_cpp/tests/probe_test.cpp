#include <gtest/gtest.h>

#include <iostream>
#include <utility>

#include "probe.hpp"

using cr::Probe;

namespace {

// (A) return the NAME of a local. NRVO applies.
Probe makeNrvo() {
    Probe local{"nrvo"};
    return local;
}

// (B) return std::move(local). The expression is now an xvalue of type
// Probe&&, not the name of a local -- so NRVO is DISABLED.
//
// NOTE: -Wall already catches this (-Wpessimizing-move) and -Werror would
// refuse to build it. We silence it HERE, deliberately, precisely so we can
// measure the damage. Read that again: the Week 0 toolchain was already
// trying to stop you writing this.
Probe makePessimized() {
    Probe local{"pessimized"};
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpessimizing-move"
    return std::move(local);
#pragma GCC diagnostic pop
}

}  // namespace

// ---------------------------------------------------------------- Q5
TEST(W1ValueCategories, NrvoCostsNothing) {
    Probe::reset();
    std::cout << "\n--- (A)  Probe p = makeNrvo();\n";

    Probe p = makeNrvo();

    EXPECT_EQ(Probe::counters().value_ctor, 1);
    EXPECT_EQ(Probe::counters().moves(),    0);   // <-- ZERO. Constructed in place.
    EXPECT_EQ(Probe::counters().copies(),   0);
}

TEST(W1ValueCategories, StdMoveOnReturnMakesItSlower) {
    Probe::reset();
    std::cout << "\n--- (B)  Probe p = makePessimized();\n";

    Probe p = makePessimized();

    EXPECT_EQ(Probe::counters().value_ctor, 1);
    EXPECT_EQ(Probe::counters().moves(),    1);   // <-- the price of std::move
    EXPECT_EQ(Probe::counters().copies(),   0);
}

// ---------------------------------------------------------------- Q4
TEST(W1ValueCategories, NamedRvalueRefIsAnLvalue) {
    Probe::reset();
    Probe   a{"a"};
    Probe&& r = std::move(a);        // type is Probe&& ...

    std::cout << "\n--- Probe b = r;   (r has a NAME)\n";
    Probe b = r;                     // ... but the EXPRESSION r is an lvalue
    EXPECT_EQ(Probe::counters().copy_ctor, 1);   // so this COPIES
    EXPECT_EQ(Probe::counters().move_ctor, 0);

    std::cout << "\n--- Probe c = std::move(r);\n";
    Probe c = std::move(r);          // now, and only now, it moves
    EXPECT_EQ(Probe::counters().move_ctor, 1);
}

// ---------------------------------------------------------------- moved-from
TEST(W1ValueCategories, MovedFromStateIsVisible) {
    Probe a{"a"};
    Probe b = std::move(a);

    EXPECT_EQ(b.name(), "a");
    EXPECT_EQ(a.name(), "<moved-from>");   // valid, but unspecified in general
}
