#include <gtest/gtest.h>
#include "page/page.hpp"
#include <type_traits>
#include "page/btree_page.hpp"

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


TEST(BPlusTreePageTest, InitSetsFields) {
    BPlusTreePage p;
    p.Init(7, 3, true, 10);

    EXPECT_EQ(p.GetPageId(),       7u);
    EXPECT_EQ(p.GetParentPageId(), 3u);
    EXPECT_TRUE(p.IsLeaf());
    EXPECT_EQ(p.GetMaxSize(),      10u);
    EXPECT_EQ(p.GetSize(),         0u);
}

TEST(BPlusTreePageTest, IsRootWhenNoParent) {
    BPlusTreePage p;
    p.Init(0, INVALID_PAGE_ID, false, 4);
    EXPECT_TRUE(p.IsRootPage());
}

TEST(BPlusTreePageTest, IsNotRootWhenHasParent) {
    BPlusTreePage p;
    p.Init(1, 0, false, 4);
    EXPECT_FALSE(p.IsRootPage());
}

TEST(BPlusTreePageTest, IsFullAtMaxSize) {
    BPlusTreePage p;
    p.Init(0, INVALID_PAGE_ID, true, 4);
    p.SetSize(4);
    EXPECT_TRUE(p.IsFull());
}

TEST(BPlusTreePageTest, IsUnderflow) {
    BPlusTreePage p;
    p.Init(0, INVALID_PAGE_ID, true, 4);
    p.SetSize(1);           // 1 < 4/2 = 2
    EXPECT_TRUE(p.IsUnderflow());
}

TEST(BPlusTreePageTest, IncreaseSizeWorks) {
    BPlusTreePage p;
    p.Init(0, INVALID_PAGE_ID, true, 10);
    p.IncreaseSize(3);
    EXPECT_EQ(p.GetSize(), 3u);
    p.IncreaseSize(-1);
    EXPECT_EQ(p.GetSize(), 2u);
}

TEST(BPlusTreePageTest, ExactlySizeBytes) {
    static_assert(sizeof(BPlusTreePage) == 20,
        "BPlusTreePage must be exactly 20 bytes");
    SUCCEED();
}
