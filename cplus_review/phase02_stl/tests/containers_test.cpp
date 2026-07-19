#include <gtest/gtest.h>

#include <array>
#include <deque>
#include <vector>

namespace {

std::uintptr_t addr(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

}  // namespace

// ================= 1. reserve prevents reallocation → pointer stays valid
TEST(W12Containers, NoReallocWhenCapacityReserved) {
    std::vector<int> v = {1, 2, 3};
    v.reserve(100);
    const int* buffer_before = v.data();

    v.push_back(4);                       // size 3 -> 4, capacity 100: NO realloc

    EXPECT_EQ(v.data(), buffer_before);   // same buffer — nothing moved
    EXPECT_EQ(*buffer_before, 1);         // safe to read
}

// ================= 2. exceeding capacity reallocates → buffer moves
TEST(W12Containers, ReallocMovesTheBuffer) {
    std::vector<int> v = {1, 2, 3};
    v.shrink_to_fit();                    // capacity == size == 3, no slack
    const int* buffer_before = v.data();

    v.push_back(4);                       // capacity exceeded -> REALLOC

    EXPECT_NE(v.data(), buffer_before);   // buffer moved to a new address
    // buffer_before is now dangling; reading *buffer_before would be use-after-free.
    EXPECT_EQ(v[0], 1);                   // data survived, at a new location
}

// ================= 3. capacity growth is geometric (W5 revisited)
TEST(W12Containers, CapacityGrowsGeometrically) {
    std::vector<int> v;
    std::size_t reallocs = 0;
    const int* last = nullptr;

    for (int i = 0; i < 1000; ++i) {
        v.push_back(i);
        if (v.data() != last) { ++reallocs; last = v.data(); }
    }
    EXPECT_LT(reallocs, 15u);             // ~log2(1000), not 1000
}

// ================= 4. deque: push_front is O(1) and does NOT move existing elements
TEST(W12Containers, DequePushFrontKeepsElementPointersValid) {
    std::deque<int> d = {10, 20, 30};
    const int* p_to_20 = &d[1];           // pointer into the middle

    for (int i = 0; i < 100; ++i) {
        d.push_front(-i);                 // vector can't do this in O(1)
    }

    EXPECT_EQ(*p_to_20, 20);              // element pointer still valid — chunks don't move
    EXPECT_EQ(d.front(), -99);
    EXPECT_EQ(d.back(), 30);
}

// ================= 5. array: fixed size, lives on the stack, never reallocates
TEST(W12Containers, ArrayIsFixedAndContiguous) {
    std::array<int, 4> a = {1, 2, 3, 4};

    EXPECT_EQ(a.size(), 4u);
    EXPECT_EQ(sizeof(a), 4 * sizeof(int));   // exactly the data, no bookkeeping
    EXPECT_EQ(addr(&a[3]) - addr(&a[0]), 3 * sizeof(int));  // contiguous
}

// ================= 6. erase in the middle invalidates from that point on
TEST(W12Containers, EraseReturnsNextValidIterator) {
    std::vector<int> v = {1, 2, 3, 4, 5};

    // Remove all even numbers safely, using erase's returned iterator.
    for (auto it = v.begin(); it != v.end(); ) {
        if (*it % 2 == 0) {
            it = v.erase(it);            // erase invalidates it; returns the next valid one
        } else {
            ++it;
        }
    }
    EXPECT_EQ(v, (std::vector<int>{1, 3, 5}));
}
