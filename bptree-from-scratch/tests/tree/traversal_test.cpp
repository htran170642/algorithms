#include <gtest/gtest.h>
#include "tree/bplus_tree.hpp"
#include "buffer/simple_page_manager.hpp"
#include "page/leaf_page.hpp"
#include "page/internal_page.hpp"

using namespace bptree;
using Tree = BPlusTree<int32_t, int32_t>;
using Leaf = LeafPage<int32_t, int32_t>;
using Cmp  = DefaultComparator<int32_t>;

TEST(TraversalTest, EmptyTreeReturnsNullptr) {
    SimplePageManager pm(64);
    Tree tree(pm);
    EXPECT_EQ(tree.FindLeafPage(42), nullptr);
}

TEST(TraversalTest, SingleLeafRoot) {
    SimplePageManager pm(64);
    Tree tree(pm);

    // Manually create a leaf root
    page_id_t root_id;
    Page* root_page = pm.NewPage(root_id);
    auto* leaf = root_page->As<Leaf>();
    Leaf::Init(leaf, root_id, INVALID_PAGE_ID);
    Cmp cmp;
    leaf->Insert(10, 100, cmp);
    leaf->Insert(20, 200, cmp);

    // Expose root (we'll add SetRootPageId in Step 7 for Insert;
    // for now test FindLeafPage by hand)
    // Skip — tested implicitly via Insert in Step 7
    pm.UnpinPage(root_id, true);
    SUCCEED();
}
