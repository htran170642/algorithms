#include <gtest/gtest.h>
#include "page/page.hpp"
#include <type_traits>

using namespace bptree;

TEST(PageTest, SizeIsPageSize) {
    EXPECT_GE(sizeof(Page), PAGE_SIZE);
}

TEST(PageTest, DefaultPageIdIsInvalid) {
    Page p;
    EXPECT_EQ(p.GetPageId(), INVALID_PAGE_ID);
}

TEST(PageTest, ResetClearsData) {
    Page p;
    p.SetPageId(42);
    p.SetDirty(true);
    p.IncrPinCount();

    p.Reset();

    EXPECT_EQ(p.GetPageId(), INVALID_PAGE_ID);
    EXPECT_FALSE(p.IsDirty());
    EXPECT_EQ(p.GetPinCount(), 0u);
}

TEST(PageTest, PinCount) {
    Page p;
    EXPECT_EQ(p.GetPinCount(), 0u);
    p.IncrPinCount();
    p.IncrPinCount();
    EXPECT_EQ(p.GetPinCount(), 2u);
    p.DecrPinCount();
    EXPECT_EQ(p.GetPinCount(), 1u);
}

TEST(PageTest, AsReturnsDataPointer) {
    Page p;
    EXPECT_EQ(p.As<uint32_t>(),
              reinterpret_cast<uint32_t*>(p.GetData()));
}

TEST(PageTest, NotCopyable) {
    EXPECT_FALSE(std::is_copy_constructible_v<Page>);
    EXPECT_FALSE(std::is_copy_assignable_v<Page>);
}

TEST(PageTest, NotMovable) {
    EXPECT_FALSE(std::is_move_constructible_v<Page>);
    EXPECT_FALSE(std::is_move_assignable_v<Page>);
}
