#include <gtest/gtest.h>

#include <string>
#include <unordered_map>

#include "hash_map.hpp"

using cr::HashMap;

// ================= 1. basic insert / find / overwrite
TEST(W14HashMap, InsertFindOverwrite) {
    HashMap<std::string, int> m;
    m.insert("hello", 1);
    m.insert("world", 2);

    EXPECT_EQ(m.find("hello"), 1);
    EXPECT_EQ(m.find("world"), 2);
    EXPECT_EQ(m.find("missing"), std::nullopt);   // absent key -> nullopt

    m.insert("hello", 99);                         // overwrite, not duplicate
    EXPECT_EQ(m.find("hello"), 99);
    EXPECT_EQ(m.size(), 2u);
}

// ================= 2. collisions: many keys, all retrievable
TEST(W14HashMap, HandlesManyKeys) {
    HashMap<int, int> m;
    for (int i = 0; i < 1000; ++i) {
        m.insert(i, i * i);
    }
    EXPECT_EQ(m.size(), 1000u);
    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(m.find(i), i * i);
    }
    EXPECT_EQ(m.find(1000), std::nullopt);
}

// ================= 3. erase, and the tombstone rule
TEST(W14HashMap, EraseKeepsLaterKeysReachable) {
    HashMap<int, int> m(8);
    // Force a probe chain: keys that collide land in consecutive slots.
    m.insert(0, 100);
    m.insert(8, 200);   // 8 & 7 == 0 -> collides with 0
    m.insert(16, 300);  // 16 & 7 == 0 -> collides again

    EXPECT_TRUE(m.erase(8));           // erase the MIDDLE of the chain
    EXPECT_EQ(m.find(8), std::nullopt);
    EXPECT_EQ(m.find(16), 300);        // must still be reachable (tombstone!)
    EXPECT_EQ(m.find(0), 100);
}

// ================= 4. rehash preserves everything
TEST(W14HashMap, RehashPreservesData) {
    HashMap<int, int> m(4);            // tiny -> forces several rehashes
    for (int i = 0; i < 100; ++i) {
        m.insert(i, i + 1);
    }
    EXPECT_GT(m.capacity(), 4u);       // it grew
    EXPECT_LE(m.loadFactor(), 0.7);    // stayed sparse
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(m.find(i), i + 1);   // nothing lost across rehashes
    }
}

// ================= 5. matches std::unordered_map's behaviour
TEST(W14HashMap, AgreesWithStdUnorderedMap) {
    HashMap<int, int>              ours;
    std::unordered_map<int, int>   std_map;

    for (int i = 0; i < 500; ++i) {
        ours.insert(i * 7, i);
        std_map[i * 7] = i;
    }
    for (int i = 0; i < 500; ++i) {
        EXPECT_EQ(ours.find(i * 7).value(), std_map[i * 7]);
    }
}
