#include <gtest/gtest.h>

#include <string>

#include "string_view.hpp"

using cr::StringView;

// ================= 1. no allocation: a view is just {ptr, len}
TEST(W18StringView, IsJustPointerAndLength) {
    EXPECT_EQ(sizeof(StringView), sizeof(const char*) + sizeof(std::size_t));  // 16

    StringView sv = "hello";
    EXPECT_EQ(sv.size(), 5u);
    EXPECT_EQ(sv[0], 'h');
    EXPECT_EQ(sv.front(), 'h');
    EXPECT_EQ(sv.back(), 'o');
}

// ================= 2. view into a std::string shares its buffer (no copy)
TEST(W18StringView, ViewsStringBufferWithoutCopying) {
    std::string s = "hello world";
    StringView sv = s;

    EXPECT_EQ(sv.data(), s.data());     // SAME pointer — no copy happened
    EXPECT_EQ(sv.size(), s.size());
}

// ================= 3. substr allocates nothing — same buffer
TEST(W18StringView, SubstrIsAnotherViewSameBuffer) {
    StringView sv = "hello world";
    StringView hello = sv.substr(0, 5);

    EXPECT_EQ(hello, StringView("hello"));
    EXPECT_EQ(hello.data(), sv.data());       // points into the SAME memory
}

// ================= 4. constexpr: fully usable at compile time
TEST(W18StringView, WorksAtCompileTime) {
    constexpr StringView sv = "compile time";
    static_assert(sv.size() == 12);
    static_assert(sv.startsWith("compile"));
    static_assert(sv[0] == 'c');
    SUCCEED();
}

// ================= 5. comparison and startsWith
TEST(W18StringView, CompareAndPrefix) {
    EXPECT_EQ(StringView("abc"), StringView("abc"));
    EXPECT_FALSE(StringView("abc") == StringView("abd"));
    EXPECT_TRUE(StringView("hello world").startsWith("hello"));
    EXPECT_FALSE(StringView("hi").startsWith("hello"));   // prefix longer than string
}
