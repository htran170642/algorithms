#include <gtest/gtest.h>

#include <iostream>
#include <vector>

#include "probe.hpp"

using cr::Probe;

// The SAME type as Probe, except the move constructor is NOT noexcept.
// One keyword different. Watch what std::vector does about it.
namespace {

class FragileProbe {
public:
    FragileProbe() = default;

    FragileProbe(const FragileProbe&) { ++copies; }
    FragileProbe(FragileProbe&&) { ++moves; }        // <-- NO noexcept

    FragileProbe& operator=(const FragileProbe&) { ++copies; return *this; }
    FragileProbe& operator=(FragileProbe&&) { ++moves; return *this; }
    ~FragileProbe() = default;

    static inline int copies = 0;
    static inline int moves  = 0;
    static void reset() { copies = 0; moves = 0; }
};

class StrongProbe {
public:
    StrongProbe() = default;

    StrongProbe(const StrongProbe&) { ++copies; }
    StrongProbe(StrongProbe&&) noexcept { ++moves; }  // <-- noexcept

    StrongProbe& operator=(const StrongProbe&) { ++copies; return *this; }
    StrongProbe& operator=(StrongProbe&&) noexcept { ++moves; return *this; }
    ~StrongProbe() = default;

    static inline int copies = 0;
    static inline int moves  = 0;
    static void reset() { copies = 0; moves = 0; }
};

}  // namespace

TEST(W1Noexcept, WithoutNoexceptVectorCopies) {
    FragileProbe::reset();
    std::vector<FragileProbe> v;
    v.reserve(1);
    v.emplace_back();
    v.emplace_back();          // forces a reallocation

    std::cout << "\nFragile (move NOT noexcept):  copies=" << FragileProbe::copies
              << "  moves=" << FragileProbe::moves << "\n";

    EXPECT_GT(FragileProbe::copies, 0);   // it COPIED the existing element
    EXPECT_EQ(FragileProbe::moves,  0);
}

TEST(W1Noexcept, WithNoexceptVectorMoves) {
    StrongProbe::reset();
    std::vector<StrongProbe> v;
    v.reserve(1);
    v.emplace_back();
    v.emplace_back();          // same reallocation

    std::cout << "Strong  (move IS noexcept):    copies=" << StrongProbe::copies
              << "  moves=" << StrongProbe::moves << "\n";

    EXPECT_EQ(StrongProbe::copies, 0);
    EXPECT_GT(StrongProbe::moves,  0);    // it MOVED
}
