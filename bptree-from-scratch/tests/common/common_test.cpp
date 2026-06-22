#include <gtest/gtest.h>
#include "common/types.hpp"
#include "common/config.hpp"
#include "tree/comparator.hpp"
#include <string>

using namespace bptree;

// --- types.hpp ---

TEST(TypesTest, InvalidPageId) {
    EXPECT_EQ(INVALID_PAGE_ID, std::numeric_limits<page_id_t>::max());
}

TEST(TypesTest, InvalidFrameId) {
    EXPECT_EQ(INVALID_FRAME_ID, std::numeric_limits<frame_id_t>::max());
}

// --- config.hpp ---

TEST(ConfigTest, PageSize) {
    EXPECT_EQ(PAGE_SIZE, 4096u);
}

// --- comparator.hpp ---

TEST(ComparatorTest, IntLessThan) {
    DefaultComparator<int32_t> cmp;
    EXPECT_LT(cmp(3, 5), 0);
}

TEST(ComparatorTest, IntGreaterThan) {
    DefaultComparator<int32_t> cmp;
    EXPECT_GT(cmp(5, 3), 0);
}

TEST(ComparatorTest, IntEqual) {
    DefaultComparator<int32_t> cmp;
    EXPECT_EQ(cmp(3, 3), 0);
}

TEST(ComparatorTest, StringLessThan) {
    DefaultComparator<std::string> cmp;
    EXPECT_LT(cmp("apple", "banana"), 0);
}

TEST(ComparatorTest, StringEqual) {
    DefaultComparator<std::string> cmp;
    EXPECT_EQ(cmp("hello", "hello"), 0);
}
