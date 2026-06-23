#include <gtest/gtest.h>
#include "page/internal_page.hpp"
#include "tree/comparator.hpp"
#include <array>

using namespace bptree;
using Internal = InternalPage<int32_t>;
using Cmp      = DefaultComparator<int32_t>;

// Helper: create an InternalPage from a stack buffer
static Internal* MakeInternal(std::array<std::byte, PAGE_SIZE>& buf,
                               page_id_t id, page_id_t parent = INVALID_PAGE_ID) {
    buf.fill(std::byte{0});
    auto* node = reinterpret_cast<Internal*>(buf.data());
    Internal::Init(node, id, parent);
    return node;
}

// Helper: manually set up [_, P0 | 10, P1 | 30, P2] with size=3
static void FillThreeChildren(Internal* node) {
    node->SetValueAt(0, 100); // P0 — leftmost child
    node->SetKeyAt(1, 10);   node->SetValueAt(1, 200); // P1
    node->SetKeyAt(2, 30);   node->SetValueAt(2, 300); // P2
    node->SetSize(3);
}

// --- Init ---

TEST(InternalPageTest, InitSetsFields) {
    std::array<std::byte, PAGE_SIZE> buf{};
    auto* node = MakeInternal(buf, 5, INVALID_PAGE_ID);

    EXPECT_EQ(node->GetPageId(),       5u);
    EXPECT_EQ(node->GetParentPageId(), INVALID_PAGE_ID);
    EXPECT_FALSE(node->IsLeaf());
    EXPECT_EQ(node->GetSize(),         0u);
}

TEST(InternalPageTest, FitsInOnePage) {
    EXPECT_LE(sizeof(Internal), PAGE_SIZE);
}

// --- Lookup ---

TEST(InternalPageTest, LookupLeftmost) {
    std::array<std::byte, PAGE_SIZE> buf{};
    auto* node = MakeInternal(buf, 1);
    FillThreeChildren(node);
    Cmp cmp;

    // key < 10 → leftmost child P0
    EXPECT_EQ(node->Lookup(5, cmp), 100u);
}

TEST(InternalPageTest, LookupMiddle) {
    std::array<std::byte, PAGE_SIZE> buf{};
    auto* node = MakeInternal(buf, 1);
    FillThreeChildren(node);
    Cmp cmp;

    // 10 <= key < 30 → P1
    EXPECT_EQ(node->Lookup(10, cmp), 200u);
    EXPECT_EQ(node->Lookup(20, cmp), 200u);
}

TEST(InternalPageTest, LookupRightmost) {
    std::array<std::byte, PAGE_SIZE> buf{};
    auto* node = MakeInternal(buf, 1);
    FillThreeChildren(node);
    Cmp cmp;

    // key >= 30 → P2
    EXPECT_EQ(node->Lookup(30, cmp), 300u);
    EXPECT_EQ(node->Lookup(99, cmp), 300u);
}

// --- InsertNodeAfter ---

TEST(InternalPageTest, InsertNodeAfter) {
    std::array<std::byte, PAGE_SIZE> buf{};
    auto* node = MakeInternal(buf, 1);
    FillThreeChildren(node); // [_, P0 | 10, P1 | 30, P2]
    Cmp cmp;

    // P1 splits → insert separator=20 and new child P1_new=250 after P1
    node->InsertNodeAfter(200, 20, 250);

    EXPECT_EQ(node->GetSize(),    4u);
    EXPECT_EQ(node->KeyAt(2),     20);
    EXPECT_EQ(node->ValueAt(2),   250u);
    EXPECT_EQ(node->KeyAt(3),     30);
    EXPECT_EQ(node->ValueAt(3),   300u);
    // Lookup still correct after insert
    EXPECT_EQ(node->Lookup(15, cmp), 200u); // P1
    EXPECT_EQ(node->Lookup(25, cmp), 250u); // P1_new
}

// --- RemoveChild ---

TEST(InternalPageTest, RemoveChild) {
    std::array<std::byte, PAGE_SIZE> buf{};
    auto* node = MakeInternal(buf, 1);
    FillThreeChildren(node); // [_, P0 | 10, P1 | 30, P2]

    node->RemoveChild(200); // remove P1

    EXPECT_EQ(node->GetSize(),  2u);
    EXPECT_EQ(node->KeyAt(1),   30);
    EXPECT_EQ(node->ValueAt(1), 300u);
}

// --- MoveHalfTo ---

TEST(InternalPageTest, MoveHalfTo) {
    std::array<std::byte, PAGE_SIZE> buf1{}, buf2{};
    auto* left  = MakeInternal(buf1, 1);
    auto* right = MakeInternal(buf2, 2);

    // [_, P0 | 10, P1 | 20, P2 | 30, P3 | 40, P4]  size=5
    left->SetValueAt(0, 100);
    left->SetKeyAt(1, 10); left->SetValueAt(1, 200);
    left->SetKeyAt(2, 20); left->SetValueAt(2, 300);
    left->SetKeyAt(3, 30); left->SetValueAt(3, 400);
    left->SetKeyAt(4, 40); left->SetValueAt(4, 500);
    left->SetSize(5);

    int32_t push_up_key = 0;
    left->MoveHalfTo(right, push_up_key);

    // middle = array_[2] = {20, P2} → push_up_key=20
    EXPECT_EQ(push_up_key,      20);
    EXPECT_EQ(left->GetSize(),  2u);
    EXPECT_EQ(right->GetSize(), 3u);

    // left keeps [_, P0 | 10, P1]
    EXPECT_EQ(left->ValueAt(0), 100u);
    EXPECT_EQ(left->KeyAt(1),   10);

    // right gets [_, P2 | 30, P3 | 40, P4]
    EXPECT_EQ(right->ValueAt(0), 300u); // P2 = child of middle entry
    EXPECT_EQ(right->KeyAt(1),   30);
    EXPECT_EQ(right->KeyAt(2),   40);
}

// --- MoveAllTo ---

TEST(InternalPageTest, MoveAllTo) {
    std::array<std::byte, PAGE_SIZE> buf1{}, buf2{};
    auto* left  = MakeInternal(buf1, 1);
    auto* right = MakeInternal(buf2, 2);

    // left:  [_, P0 | 10, P1]  size=2
    left->SetValueAt(0, 100);
    left->SetKeyAt(1, 10); left->SetValueAt(1, 200);
    left->SetSize(2);

    // right: [_, P2 | 30, P3]  size=2
    right->SetValueAt(0, 300);
    right->SetKeyAt(1, 30); right->SetValueAt(1, 400);
    right->SetSize(2);

    // parent_key = 20 (separator between left and right)
    right->MoveAllTo(left, 20);

    // left: [_, P0 | 10, P1 | 20, P2 | 30, P3]  size=4
    EXPECT_EQ(left->GetSize(),   4u);
    EXPECT_EQ(left->KeyAt(2),    20);
    EXPECT_EQ(left->ValueAt(2),  300u);
    EXPECT_EQ(left->KeyAt(3),    30);
    EXPECT_EQ(right->GetSize(),  0u);
}
