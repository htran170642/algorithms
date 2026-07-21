#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <random>
#include <vector>

#include "avl_tree.hpp"

using cr::AvlTree;

// ================= 1. basic insert / find
TEST(W15Avl, InsertFind) {
    AvlTree<int, int> t;
    t.insert(5, 50);
    t.insert(3, 30);
    t.insert(8, 80);

    EXPECT_EQ(t.find(5), 50);
    EXPECT_EQ(t.find(3), 30);
    EXPECT_EQ(t.find(8), 80);
    EXPECT_EQ(t.find(99), std::nullopt);
    EXPECT_EQ(t.size(), 3u);
}

// ================= 2. in-order traversal yields SORTED keys (hash can't)
TEST(W15Avl, InOrderIsSorted) {
    AvlTree<int, int> t;
    for (int k : {5, 2, 8, 1, 9, 3, 7}) {
        t.insert(k, k);
    }
    EXPECT_EQ(t.sortedKeys(), (std::vector<int>{1, 2, 3, 5, 7, 8, 9}));
}

// ================= 3. THE POINT: sorted insertion does NOT degenerate
TEST(W15Avl, SortedInsertionStaysBalanced) {
    AvlTree<int, int> t;
    const int n = 100000;
    for (int i = 0; i < n; ++i) {
        t.insert(i, i);                 // ascending — worst case for a plain BST
    }

    EXPECT_EQ(t.size(), static_cast<std::size_t>(n));

    // A degenerate BST would have height == n (100000). AVL keeps it ~1.44*log2(n).
    int max_allowed = static_cast<int>(2.0 * std::log2(n)) + 2;   // ~35
    EXPECT_LE(t.height(), max_allowed);
    EXPECT_GT(t.height(), 0);
}

// ================= 4. overwrite, not duplicate
TEST(W15Avl, InsertExistingOverwrites) {
    AvlTree<int, int> t;
    t.insert(1, 100);
    t.insert(1, 200);
    EXPECT_EQ(t.find(1), 200);
    EXPECT_EQ(t.size(), 1u);
}

// ================= 5. differential test against std::map, random keys
TEST(W15Avl, AgreesWithStdMap) {
    AvlTree<int, int>   ours;
    std::map<int, int>  ref;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 10000);
    for (int i = 0; i < 5000; ++i) {
        int k = dist(rng);
        ours.insert(k, i);
        ref[k] = i;
    }

    // same size, same sorted key order, same values
    EXPECT_EQ(ours.size(), ref.size());

    std::vector<int> ref_keys;
    for (const auto& [k, v] : ref) ref_keys.push_back(k);
    EXPECT_EQ(ours.sortedKeys(), ref_keys);

    for (const auto& [k, v] : ref) {
        EXPECT_EQ(ours.find(k), v);
    }
}
