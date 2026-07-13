#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <vector>

#include "probe.hpp"

using cr::Probe;

// ============================================================ 1. The brace trap
TEST(W2Init, ParensVsBracesAreDifferentVectors) {
    std::vector<int> a(10, 5);    // (count, value)
    std::vector<int> b{10, 5};    // initializer_list -> literally {10, 5}

    EXPECT_EQ(a.size(), 10u);
    EXPECT_EQ(b.size(), 2u);

    EXPECT_EQ(a[0], 5);
    EXPECT_EQ(b[0], 10);          // <-- 10, not 5
    EXPECT_EQ(b[1], 5);
}

TEST(W2Init, TheCruelestPair) {
    std::vector<int> v1(3);       // three zeroes
    std::vector<int> v2{3};       // one element, the number 3

    EXPECT_EQ(v1.size(), 3u);
    EXPECT_EQ(v2.size(), 1u);
    EXPECT_EQ(v2[0], 3);
}

// ==================================== 2. initializer_list wins, brutally
namespace {

enum class Chosen { IntInt, InitList };

struct Greedy {
    Chosen chosen;

    Greedy(int, int) : chosen(Chosen::IntInt) {}
    Greedy(std::initializer_list<int>) : chosen(Chosen::InitList) {}
};

}  // namespace

TEST(W2Init, InitializerListSwallowsEverything) {
    Greedy parens(1, 2);
    Greedy braces{1, 2};

    EXPECT_EQ(parens.chosen, Chosen::IntInt);
    EXPECT_EQ(braces.chosen, Chosen::InitList);   // even though (int,int) is a perfect match
}

// ============================================== 3. explicit stops silent nonsense
namespace {

struct Implicit {
    int size;
    Implicit(int s) : size(s) {}            // NOT explicit — a trapdoor
};

struct Explicit {
    int size;
    explicit Explicit(int s) : size(s) {}   // the door is now locked
};

int takesImplicit(Implicit b) { return b.size; }
int takesExplicit(Explicit b) { return b.size; }

}  // namespace

TEST(W2Init, ImplicitConversionHappensSilently) {
    EXPECT_EQ(takesImplicit(42), 42);       // 42 silently became an Implicit{42}

    // takesExplicit(42);                   // <-- UNCOMMENT ME. It will not compile.
    EXPECT_EQ(takesExplicit(Explicit{42}), 42);   // you must say what you mean
}

// ======================================== 4. default- vs value-initialization
namespace {

struct Pod {
    int  a;
    bool b;
};

}  // namespace

TEST(W2Init, DefaultInitLeavesGarbageValueInitZeroes) {
    Pod value{};          // value-init  -> zeroed
    EXPECT_EQ(value.a, 0);
    EXPECT_EQ(value.b, false);

    // Pod garbage;       // default-init -> a and b are INDETERMINATE.
    // EXPECT_EQ(garbage.a, 0);   // reading them is UB. MSan/valgrind would scream.
}

// ======================================== 5. designated initializers (C++20)
namespace {

struct Config {
    int         port    = 8080;
    bool        verbose = false;
    std::string host    = "localhost";
};

}  // namespace

TEST(W2Init, DesignatedInitializersNameTheFields) {
    Config c{.port = 9000, .verbose = true};   // host keeps its default

    EXPECT_EQ(c.port, 9000);
    EXPECT_TRUE(c.verbose);
    EXPECT_EQ(c.host, "localhost");
}

// ============================ 6. delegating ctors — write the invariant ONCE
namespace {

class Connection {
public:
    Connection(std::string host, int port) : host_(std::move(host)), port_(port) {
        if (port_ <= 0) {
            throw std::invalid_argument("port must be positive");
        }
    }

    // Delegates. The validation above is NOT duplicated -- and cannot drift.
    explicit Connection(std::string host) : Connection(std::move(host), 8080) {}

    [[nodiscard]] const std::string& host() const noexcept { return host_; }
    [[nodiscard]] int port() const noexcept { return port_; }

private:
    std::string host_;
    int         port_;
};

}  // namespace

TEST(W2Init, DelegatingCtorReusesValidation) {
    Connection c{"example.com"};
    EXPECT_EQ(c.host(), "example.com");
    EXPECT_EQ(c.port(), 8080);

    EXPECT_THROW(Connection("bad", -1), std::invalid_argument);
}
