#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <utility>

#include "probe.hpp"
#include "scoped_file.hpp"

using cr::Probe;
using cr::ScopedFile;

// ==================================================== 1. RAII survives exceptions
TEST(W3Raii, HandleIsReleasedEvenWhenAnExceptionUnwinds) {
    const std::string path = "/tmp/cr_w3_raii.txt";

    EXPECT_THROW(
        {
            ScopedFile f(path, "w");
            f.write("hello");
            throw std::runtime_error("boom");   // ~ScopedFile() still runs
        },
        std::runtime_error);

    EXPECT_TRUE(std::filesystem::exists(path));
    std::filesystem::remove(path);
}

TEST(W3Raii, OpeningAMissingFileThrows) {
    EXPECT_THROW(ScopedFile("/definitely/not/here.txt", "r"), std::runtime_error);
}

TEST(W3Raii, MoveTransfersOwnership) {
    ScopedFile a("/tmp/cr_w3_move.txt", "w");
    EXPECT_TRUE(a.valid());

    ScopedFile b = std::move(a);
    EXPECT_TRUE(b.valid());
    EXPECT_FALSE(a.valid());        // a was gutted -- and won't double-close

    std::filesystem::remove("/tmp/cr_w3_move.txt");
}

// ============ 2. Constructor throws => destructor NEVER runs (but members die)
namespace {

struct Boom {
    Boom() { throw std::runtime_error("ctor blew up"); }
};

class HalfBuilt {
public:
    HalfBuilt() = default;
    ~HalfBuilt() { dtor_ran = true; }

    static inline bool dtor_ran = false;

private:
    Probe a_{"a"};     // constructed OK
    Probe b_{"b"};     // constructed OK
    Boom  c_;          // <-- throws. HalfBuilt never finishes existing.
};

}  // namespace

TEST(W3Raii, CtorThrowsSoDtorNeverRunsButMembersAreDestroyed) {
    HalfBuilt::dtor_ran = false;
    Probe::reset();
    Probe::setVerbose(false);

    EXPECT_THROW(HalfBuilt{}, std::runtime_error);

    EXPECT_FALSE(HalfBuilt::dtor_ran);            // the object never existed
    EXPECT_EQ(Probe::counters().dtor, 2);         // but a_ and b_ WERE cleaned up
}

// ================= 3. THE TRAP: declaring a destructor kills move semantics
namespace {

struct RuleOfZero {          // declares NOTHING. Compiler generates all five.
    Probe p_{"zero"};
};

struct HasDtor {             // one "harmless" destructor...
    Probe p_{"dtor"};
    ~HasDtor() {}            // ...and move is silently gone
};

}  // namespace

TEST(W3Raii, RuleOfZeroKeepsTheMove) {
    Probe::reset();
    Probe::setVerbose(false);

    RuleOfZero a;
    RuleOfZero b = std::move(a);

    EXPECT_EQ(Probe::counters().move_ctor, 1);    // moved
    EXPECT_EQ(Probe::counters().copy_ctor, 0);
}

TEST(W3Raii, DeclaringADestructorSilentlyDisablesMove) {
    Probe::reset();
    Probe::setVerbose(false);

    HasDtor a;
    HasDtor b = std::move(a);      // looks like a move. Reads like a move.

    EXPECT_EQ(Probe::counters().move_ctor, 0);    // it did NOT move
    EXPECT_EQ(Probe::counters().copy_ctor, 1);    // it COPIED
}
