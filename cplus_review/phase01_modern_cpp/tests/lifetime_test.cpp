#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

namespace {

// ============================================== lifetime extension: the good case
TEST(W9Lifetime, ConstRefExtendsATemporary) {
    const std::string& r = std::string("hello");   // extended to r's lifetime
    EXPECT_EQ(r, "hello");                          // safe to read
    EXPECT_EQ(r.size(), 5u);
}

// ============================================== the trap: extension does NOT chain
TEST(W9Lifetime, ExtensionDoesNotChainThroughAMethodCall) {
    // const std::string& s = std::string("hello").substr(0, 3);
    //   -> substr returns a NEW temporary; the "hello" temporary dies at the ;.
    //   -> s would dangle. -Wdangling / ASAN would flag a read.
    //
    // The correct forms:
    std::string owned = std::string("hello").substr(0, 3);   // by value: owns a copy
    EXPECT_EQ(owned, "hel");
}

// ============================================== [[nodiscard]]
[[nodiscard]] bool tryConnect(bool ok) { return ok; }

TEST(W9Lifetime, NodiscardForcesYouToUseTheResult) {
    bool connected = tryConnect(true);   // must capture it
    EXPECT_TRUE(connected);
    // tryConnect(true);   // <-- uncomment: -Werror=unused-result fails the build
}

// ============================================== iterator invalidation preview (W12)
TEST(W9Lifetime, ReferenceIntoVectorDanglesAfterRealloc) {
    std::vector<int> v;
    v.reserve(2);
    v.push_back(10);

    int& elem = v[0];        // reference into the buffer
    EXPECT_EQ(elem, 10);

    // Force a reallocation: the old buffer (and elem) is freed.
    for (int i = 0; i < 100; ++i) {
        v.push_back(i);
    }
    // Reading `elem` here is use-after-free — ASAN would catch it.
    // We assert on the SURVIVING data instead:
    EXPECT_EQ(v[0], 10);
    EXPECT_EQ(v.size(), 101u);
}

}  // namespace
