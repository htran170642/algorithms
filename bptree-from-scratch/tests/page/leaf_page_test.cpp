#include <gtest/gtest.h>
#include "page/leaf_page.hpp"
#include "tree/comparator.hpp"
#include <array>

using namespace bptree;
using Leaf = LeafPage<int32_t, int32_t>;
using Cmp  = DefaultComparator<int32_t>;

// Helper: tạo LeafPage từ buffer stack (không cần buffer pool)
static Leaf* MakeLeaf(std::array<std::byte, PAGE_SIZE>& buf,
                      page_id_t id, page_id_t parent = INVALID_PAGE_ID) {
    buf.fill(std::byte{0});
    auto* leaf = reinterpret_cast<Leaf*>(buf.data());
    Leaf::Init(leaf, id, parent);
    return leaf;
}

// --- Size ---

TEST(LeafPageTest, MaxSizeIsPositive) {
    EXPECT_GT(Leaf::MAX_SIZE, 0u);
}

TEST(LeafPageTest, FitsInOnePage) {
    EXPECT_LE(sizeof(Leaf), PAGE_SIZE);
}

TEST(LeafPageTest, InitSetsFields) {
    std::array<std::byte, PAGE_SIZE> buf{};
    auto* leaf = MakeLeaf(buf, 1, INVALID_PAGE_ID);

    EXPECT_EQ(leaf->GetPageId(),       1u);
    EXPECT_EQ(leaf->GetParentPageId(), INVALID_PAGE_ID);
    EXPECT_TRUE(leaf->IsLeaf());
    EXPECT_EQ(leaf->GetSize(),         0u);
    EXPECT_EQ(leaf->GetNextPageId(),   INVALID_PAGE_ID);
}

// --- Insert & Lookup ---

TEST(LeafPageTest, InsertAndLookup) {
    std::array<std::byte, PAGE_SIZE> buf{};
    auto* leaf = MakeLeaf(buf, 1);
    Cmp cmp;

    EXPECT_TRUE(leaf->Insert(3, 30, cmp));
    EXPECT_TRUE(leaf->Insert(1, 10, cmp));
    EXPECT_TRUE(leaf->Insert(2, 20, cmp));

    EXPECT_EQ(leaf->GetSize(), 3u);
    EXPECT_EQ(leaf->Lookup(1, cmp), 10);
    EXPECT_EQ(leaf->Lookup(2, cmp), 20);
    EXPECT_EQ(leaf->Lookup(3, cmp), 30);
}

TEST(LeafPageTest, InsertMaintainsSortedOrder) {
    std::array<std::byte, PAGE_SIZE> buf{};
    auto* leaf = MakeLeaf(buf, 1);
    Cmp cmp;

    leaf->Insert(5, 50, cmp);
    leaf->Insert(1, 10, cmp);
    leaf->Insert(3, 30, cmp);

    EXPECT_EQ(leaf->EntryAt(0).first, 1);
    EXPECT_EQ(leaf->EntryAt(1).first, 3);
    EXPECT_EQ(leaf->EntryAt(2).first, 5);
}

TEST(LeafPageTest, InsertDuplicateReturnsFalse) {
    std::array<std::byte, PAGE_SIZE> buf{};
    auto* leaf = MakeLeaf(buf, 1);
    Cmp cmp;

    EXPECT_TRUE(leaf->Insert(1, 10, cmp));
    EXPECT_FALSE(leaf->Insert(1, 99, cmp));
    EXPECT_EQ(leaf->GetSize(), 1u);
}

TEST(LeafPageTest, LookupMissingReturnsNullopt) {
    std::array<std::byte, PAGE_SIZE> buf{};
    auto* leaf = MakeLeaf(buf, 1);
    Cmp cmp;

    leaf->Insert(1, 10, cmp);
    EXPECT_EQ(leaf->Lookup(99, cmp), std::nullopt);
}

// --- Remove ---

TEST(LeafPageTest, Remove) {
    std::array<std::byte, PAGE_SIZE> buf{};
    auto* leaf = MakeLeaf(buf, 1);
    Cmp cmp;

    leaf->Insert(1, 10, cmp);
    leaf->Insert(2, 20, cmp);
    leaf->Insert(3, 30, cmp);

    EXPECT_TRUE(leaf->Remove(2, cmp));
    EXPECT_EQ(leaf->GetSize(), 2u);
    EXPECT_EQ(leaf->Lookup(2, cmp), std::nullopt);
    // còn lại vẫn sorted
    EXPECT_EQ(leaf->EntryAt(0).first, 1);
    EXPECT_EQ(leaf->EntryAt(1).first, 3);
}

TEST(LeafPageTest, RemoveMissingReturnsFalse) {
    std::array<std::byte, PAGE_SIZE> buf{};
    auto* leaf = MakeLeaf(buf, 1);
    Cmp cmp;

    EXPECT_FALSE(leaf->Remove(99, cmp));
}

// --- MoveHalfTo ---

TEST(LeafPageTest, MoveHalfTo) {
    std::array<std::byte, PAGE_SIZE> buf1{}, buf2{};
    auto* left  = MakeLeaf(buf1, 1);
    auto* right = MakeLeaf(buf2, 2);
    Cmp cmp;

    left->Insert(1, 10, cmp);
    left->Insert(2, 20, cmp);
    left->Insert(3, 30, cmp);
    left->Insert(4, 40, cmp);

    left->MoveHalfTo(right);

    EXPECT_EQ(left->GetSize(),  2u);
    EXPECT_EQ(right->GetSize(), 2u);
    // left giữ nửa đầu
    EXPECT_EQ(left->EntryAt(0).first, 1);
    EXPECT_EQ(left->EntryAt(1).first, 2);
    // right nhận nửa sau
    EXPECT_EQ(right->EntryAt(0).first, 3);
    EXPECT_EQ(right->EntryAt(1).first, 4);
    // linked list: left → right → INVALID
    EXPECT_EQ(left->GetNextPageId(),  2u);
    EXPECT_EQ(right->GetNextPageId(), INVALID_PAGE_ID);
}

// --- MoveAllTo ---

TEST(LeafPageTest, MoveAllTo) {
    std::array<std::byte, PAGE_SIZE> buf1{}, buf2{};
    auto* left  = MakeLeaf(buf1, 1);
    auto* right = MakeLeaf(buf2, 2);
    Cmp cmp;

    left->Insert(1, 10, cmp);
    left->Insert(2, 20, cmp);
    right->Insert(3, 30, cmp);
    left->SetNextPageId(2);

    right->MoveAllTo(left);

    EXPECT_EQ(left->GetSize(),  3u);
    EXPECT_EQ(right->GetSize(), 0u);
    EXPECT_EQ(left->GetNextPageId(), INVALID_PAGE_ID);
}
