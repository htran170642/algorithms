#include <gtest/gtest.h>

#include <functional>
#include <queue>
#include <random>
#include <vector>

#include "binary_heap.hpp"

using cr::BinaryHeap;

// helper: drain a heap into a vector (pops come out in sorted order)
template <typename Heap>
std::vector<int> drain(Heap& h) {
    std::vector<int> out;
    while (!h.empty()) { out.push_back(h.top()); h.pop(); }
    return out;
}

// ================= 1. max-heap: pops come out descending
TEST(W16Heap, MaxHeapPopsDescending) {
    BinaryHeap<int> h;
    for (int x : {3, 1, 4, 1, 5, 9, 2, 6}) h.push(x);

    EXPECT_EQ(h.top(), 9);                       // max always at top
    EXPECT_EQ(drain(h), (std::vector<int>{9, 6, 5, 4, 3, 2, 1, 1}));
}

// ================= 2. min-heap via std::greater
TEST(W16Heap, MinHeapWithGreater) {
    BinaryHeap<int, std::greater<int>> h;
    for (int x : {3, 1, 4, 1, 5}) h.push(x);

    EXPECT_EQ(h.top(), 1);                        // min at top now
    EXPECT_EQ(drain(h), (std::vector<int>{1, 1, 3, 4, 5}));   // ascending
}

// ================= 3. differential test against std::priority_queue
TEST(W16Heap, AgreesWithStdPriorityQueue) {
    BinaryHeap<int>              ours;
    std::priority_queue<int>     ref;

    std::mt19937 rng(7);
    std::uniform_int_distribution<int> dist(0, 1000);
    for (int i = 0; i < 5000; ++i) {
        int x = dist(rng);
        ours.push(x);
        ref.push(x);
    }

    EXPECT_EQ(ours.size(), ref.size());
    while (!ref.empty()) {
        ASSERT_FALSE(ours.empty());
        EXPECT_EQ(ours.top(), ref.top());        // same order, every step
        ours.pop();
        ref.pop();
    }
}

// ================= 4. the heap invariant holds after every push
TEST(W16Heap, ParentAlwaysGreaterOrEqualToChildren) {
    BinaryHeap<int> h;
    std::mt19937 rng(1);
    std::uniform_int_distribution<int> dist(0, 100);
    for (int i = 0; i < 100; ++i) h.push(dist(rng));

    // draining a valid max-heap must yield a non-increasing sequence
    auto out = drain(h);
    for (std::size_t i = 1; i < out.size(); ++i) {
        EXPECT_GE(out[i - 1], out[i]);           // each >= the next
    }
}
