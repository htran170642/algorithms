#include <gtest/gtest.h>

#include <cstring>
#include <utility>

#include "string.hpp"

using cr::String;

// ================================================= 1. SSO: short strings never allocate
TEST(W4String, ShortStringDoesNotTouchTheHeap) {
    String::resetAllocs();

    String s = "hi";                       // 2 chars — fits inline

    EXPECT_EQ(String::heap_allocs, 0);     // <-- ZERO allocations
    EXPECT_TRUE(s.isShort());
    EXPECT_STREQ(s.c_str(), "hi");
    EXPECT_EQ(s.size(), 2u);
}

TEST(W4String, LongStringAllocatesOnce) {
    String::resetAllocs();

    String s = "this string is definitely longer than fifteen characters";

    EXPECT_EQ(String::heap_allocs, 1);     // one, and only one
    EXPECT_FALSE(s.isShort());
    EXPECT_EQ(s.size(), 56u);
}

TEST(W4String, TheBoundaryIsFifteenChars) {
    String::resetAllocs();
    String a = "123456789012345";          // 15 chars -> inline (16th is '\0')
    EXPECT_TRUE(a.isShort());
    EXPECT_EQ(String::heap_allocs, 0);

    String b = "1234567890123456";          // 16 chars -> heap
    EXPECT_FALSE(b.isShort());
    EXPECT_EQ(String::heap_allocs, 1);
}

// ================================================= 2. move: long steals, short copies
TEST(W4String, MovingALongStringStealsThePointerNoNewAlloc) {
    String::resetAllocs();
    String src = "this string is long enough to live on the heap for sure";
    EXPECT_EQ(String::heap_allocs, 1);

    String dst = std::move(src);            // steal — must NOT allocate again

    EXPECT_EQ(String::heap_allocs, 1);      // still 1: the pointer was stolen
    EXPECT_FALSE(dst.isShort());
    EXPECT_EQ(src.size(), 0u);              // source left valid & empty
}

// ================================================= 3. copy-and-swap
TEST(W4String, CopyAssignmentDeepCopies) {
    String a = "hello world this is long enough for heap";
    String b = "x";

    b = a;                                  // copy-assign via copy-and-swap

    EXPECT_STREQ(a.c_str(), b.c_str());
    EXPECT_NE(a.c_str(), b.c_str());        // different buffers — deep copy
}

TEST(W4String, SelfAssignmentIsSafe) {
    String s = "self assignment must not explode, even when long";

    s = s;                                  // the classic bomb

    EXPECT_STREQ(s.c_str(), "self assignment must not explode, even when long");
    EXPECT_EQ(s.size(), 48u);
}

TEST(W4String, MoveAssignmentWorksThroughTheSameOperator) {
    String::resetAllocs();
    String a = "some sufficiently long heap-backed string value here";
    String b = "y";

    b = std::move(a);                       // move-assign — same operator=

    EXPECT_FALSE(b.isShort());
    EXPECT_EQ(String::heap_allocs, 1);      // no extra alloc: the buffer moved
}
