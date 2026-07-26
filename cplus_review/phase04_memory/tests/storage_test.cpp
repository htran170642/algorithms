// W28 — proving manual lifetime is exactly one ctor and exactly one dtor.
//
// The Probe from W1 counts every ctor/dtor globally, so we can assert that
// Storage<Probe> runs the object's lifetime the RIGHT number of times — no
// double-construct, no forgotten destroy. The most important test is the
// contrast one: raw placement new WITHOUT a matching ~T() silently skips the
// destructor, which Storage's RAII exists to prevent.

#include <cstddef>
#include <new>
#include <string>
#include <type_traits>

#include <gtest/gtest.h>

#include "phase01_modern_cpp/include/probe.hpp"   // cr::Probe, reused from W1
#include "w28/storage.hpp"

namespace {

using cr::Probe;

// --- Alignment & size are for T, not just sized-for-T -------------------------
static_assert(alignof(w28::Storage<double>) >= alignof(double));
static_assert(sizeof(w28::Storage<double>)  >= sizeof(double));
static_assert(alignof(w28::Storage<Probe>)  >= alignof(Probe));

TEST(Storage, ConstructThenDestroyIsExactlyOneEach) {
    Probe::setVerbose(false);
    Probe::reset();

    w28::Storage<Probe> s;
    EXPECT_FALSE(s.engaged());

    s.construct("in-storage");
    EXPECT_TRUE(s.engaged());
    EXPECT_EQ(Probe::counters().value_ctor, 1);  // built exactly once...
    EXPECT_EQ(Probe::counters().copy_ctor, 0);   // ...in place, no copy
    EXPECT_EQ(Probe::counters().move_ctor, 0);   // ...no move either
    EXPECT_EQ(Probe::counters().dtor, 0);        // ...and not yet destroyed

    EXPECT_EQ(s.get().name(), "in-storage");

    s.destroy();
    EXPECT_FALSE(s.engaged());
    EXPECT_EQ(Probe::counters().dtor, 1);        // destroyed exactly once
}

TEST(Storage, DestructorReclaimsAnEngagedObject) {
    Probe::setVerbose(false);
    Probe::reset();
    {
        w28::Storage<Probe> s;
        s.construct("raii");
        EXPECT_EQ(Probe::counters().dtor, 0);
        // no explicit destroy() — ~Storage() must run ~Probe() for us
    }
    EXPECT_EQ(Probe::counters().value_ctor, 1);
    EXPECT_EQ(Probe::counters().dtor, 1);        // RAII closed the object
}

// --- THE lesson: raw placement new + forgotten ~T() = silent skipped dtor -----
// This is what Storage's RAII protects you from. We deliberately do it by hand
// to SEE the missing destructor: the byte buffer is reclaimed at scope exit
// (no heap leak, ASAN stays quiet), but ~Probe() never runs, so any resource
// the object held would leak. dtor count proves it.
TEST(Storage, RawPlacementNewWithoutDestroySkipsTheDestructor) {
    Probe::setVerbose(false);
    Probe::reset();
    {
        alignas(Probe) std::byte buf[sizeof(Probe)];
        Probe* p = ::new (static_cast<void*>(buf)) Probe("orphan");
        EXPECT_EQ(p->name(), "orphan");
        // Intentionally NO p->~Probe(). Buffer dies with the scope; object does not.
    }
    EXPECT_EQ(Probe::counters().value_ctor, 1);
    EXPECT_EQ(Probe::counters().dtor, 0);        // <-- the object was never destroyed
}

// --- Re-use: destroy then construct again in the same storage -----------------
TEST(Storage, ReconstructInPlace) {
    Probe::setVerbose(false);
    Probe::reset();

    w28::Storage<Probe> s;
    s.construct("first");
    s.destroy();
    s.construct("second");
    EXPECT_EQ(s.get().name(), "second");
    s.destroy();

    EXPECT_EQ(Probe::counters().value_ctor, 2);
    EXPECT_EQ(Probe::counters().dtor, 2);        // two full lifetimes, balanced
}

}  // namespace
