// W43 — Copy elision / RVO / NRVO, proven with the W1 Probe's counters.
//
// W1's probe_test already showed NRVO=0-moves and that std::move(local) on
// return is pessimizing. W43 goes UNDER that, at the Phase-6 altitude:
//
//   1. There are TWO elisions with DIFFERENT guarantees.
//      - prvalue return  -> GUARANTEED copy elision (C++17): 0 move, always.
//      - named return    -> NRVO: OPTIONAL. 0 move IF the compiler does it,
//                           else it falls back to a MOVE (never a copy).
//
//   2. We PROVE the difference by compiling this exact file TWICE:
//        - normal                     -> elision on
//        - with -fno-elide-constructors + -DCR_NO_ELIDE -> optional elision OFF
//      The flag can force NRVO off (kNrvoMoves flips 0 -> 1), but it CANNOT
//      touch guaranteed prvalue elision -- that stays 0. That asymmetry is the
//      whole lesson, and here it is a measured assertion, not a claim.
//
//   3. Where elision does NOT apply at all: returning a member, a by-value
//      parameter, one of several objects, and assigning (not initializing).
//
// Run:
//   ctest --preset debug-23 -R phase06_elision   # both variants
//   ./build/debug-23/phase06_performance/phase06_elision_noelide_test  # see NRVO move

#include <gtest/gtest.h>

#include <utility>

#include "probe.hpp"

using cr::Probe;

namespace {

// (1) Return a PRVALUE. C++17 guaranteed elision: the Probe{} IS what
// initializes the caller's object -- no temporary ever exists to move from.
Probe makePrvalue() { return Probe{"prvalue"}; }

// (2) Return a NAMED local. NRVO is allowed but not required. If the compiler
// declines (e.g. -fno-elide-constructors), `return local;` treats the expiring
// local as an rvalue -> a MOVE (not a copy). This is the ONLY case the flag
// changes.
Probe makeNrvo() {
    Probe local{"nrvo"};
    return local;
}

// (3) Two candidate objects on different return paths -> NRVO cannot pick one
// storage slot up front, so it is not applied. Each `return` still names a
// local id-expression, so the fallback is a MOVE, never a copy.
Probe makeTwoPaths(bool b) {
    Probe a{"a"};
    Probe c{"c"};
    if (b) return a;
    return c;
}

// (4) Return a MEMBER of a local. A member is not a variable, so it is not an
// implicitly-movable entity -> this COPIES, in every standard, elision or not.
struct Wrapper {
    Probe p{"member"};
};
Probe returnMember() {
    Wrapper w;
    return w.p;
}

// (5) Return a by-value PARAMETER. NRVO never applies to parameters, but a
// parameter IS implicitly movable -> a MOVE, not a copy. The prvalue argument
// still constructs `param` in place (guaranteed), so there is 0 move on the
// way IN and exactly 1 move on the way OUT.
Probe passThrough(Probe param) { return param; }

}  // namespace

// -- The headline: guaranteed vs optional, side by side --------------------

// prvalue elision is GUARANTEED -> 0 move even when -fno-elide-constructors is
// on. Same assertion in both build variants; that is the point.
TEST(W43Elision, PrvalueReturnIsGuaranteedZeroMove) {
    Probe::reset();
    Probe p = makePrvalue();
    EXPECT_EQ(Probe::counters().value_ctor, 1);
    EXPECT_EQ(Probe::counters().moves(),  0);   // guaranteed, unconditional
    EXPECT_EQ(Probe::counters().copies(), 0);
    EXPECT_EQ(p.name(), "prvalue");
}

// NRVO is OPTIONAL -> 0 move normally, but 1 move once the optimizer is told to
// stand down. Never a copy either way.
TEST(W43Elision, NrvoReturnIsOptional) {
#ifdef CR_NO_ELIDE
    constexpr int kNrvoMoves = 1;   // NRVO disabled -> move fallback
#else
    constexpr int kNrvoMoves = 0;   // NRVO applied -> nothing at all
#endif
    Probe::reset();
    Probe p = makeNrvo();
    EXPECT_EQ(Probe::counters().value_ctor, 1);
    EXPECT_EQ(Probe::counters().moves(),  kNrvoMoves);
    EXPECT_EQ(Probe::counters().copies(), 0);   // NEVER a copy
    EXPECT_EQ(p.name(), "nrvo");
}

// -- Where elision does NOT save you (stable across standards & the flag) ---

// Multiple return objects: NRVO off, but implicit move on -> exactly 1 move.
TEST(W43Elision, MultipleReturnPathsMoveNeverCopy) {
    Probe::reset();
    Probe p = makeTwoPaths(true);
    EXPECT_EQ(Probe::counters().copies(), 0);
    EXPECT_EQ(Probe::counters().move_ctor, 1);
}

// Returning a member copies -- a member is not an implicitly-movable variable.
TEST(W43Elision, ReturningAMemberCopies) {
    Probe::reset();
    Probe p = returnMember();
    EXPECT_EQ(Probe::counters().copy_ctor, 1);
    EXPECT_EQ(Probe::counters().move_ctor, 0);
    EXPECT_EQ(p.name(), "member");
}

// By-value param: prvalue arg builds `param` in place (0 move in), then the
// return moves it out (1 move) -- parameters are movable but never NRVO'd.
TEST(W43Elision, ByValueParamMovesOutNotIn) {
    Probe::reset();
    Probe p = passThrough(Probe{"through"});
    EXPECT_EQ(Probe::counters().value_ctor, 1);   // the prvalue built param directly
    EXPECT_EQ(Probe::counters().copies(),   0);
    EXPECT_EQ(Probe::counters().move_ctor,  1);   // return param;
}

// Elision only applies to INITIALIZATION. Assigning into a live object cannot
// elide -- the prvalue is built, then move-ASSIGNED in.
TEST(W43Elision, AssignmentIntoLiveObjectCannotElide) {
    Probe::reset();
    Probe existing{"existing"};
    existing = makePrvalue();                     // not init -> move-assign
    EXPECT_EQ(Probe::counters().move_assign, 1);
    EXPECT_EQ(Probe::counters().copies(),    0);
    EXPECT_EQ(existing.name(), "prvalue");
}
