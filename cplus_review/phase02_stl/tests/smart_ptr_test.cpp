#include <gtest/gtest.h>

#include <string>

#include "smart_ptr.hpp"

using cr::UniquePtr;
using cr::SharedPtr;
using cr::WeakPtr;
using cr::makeShared;

namespace {

struct Tracked {
    static inline int alive = 0;
    int id;
    explicit Tracked(int i) : id(i) { ++alive; }
    ~Tracked() { --alive; }
};

// Two nodes that can point at each other — the cycle setup.
struct Node {
    static inline int alive = 0;
    SharedPtr<Node> next;          // strong -> can form a cycle
    WeakPtr<Node>   prev_weak;     // weak   -> breaks the cycle
    SharedPtr<Node> prev_strong;   // strong -> leaks (for the demo)
    Node()  { ++alive; }
    ~Node() { --alive; }
};

}  // namespace

// ================= 1. unique_ptr: move-only, deletes on scope exit
TEST(W19Smart, UniquePtrOwnsAndDeletes) {
    Tracked::alive = 0;
    {
        UniquePtr<Tracked> p(new Tracked(1));
        EXPECT_EQ(Tracked::alive, 1);
        EXPECT_EQ(p->id, 1);

        UniquePtr<Tracked> q = std::move(p);   // transfer ownership
        EXPECT_FALSE(static_cast<bool>(p));    // p is now empty
        EXPECT_TRUE(static_cast<bool>(q));
    }
    EXPECT_EQ(Tracked::alive, 0);              // destroyed on scope exit
}

// ================= 2. shared_ptr: ref count tracks owners
TEST(W19Smart, SharedPtrCountsOwners) {
    Tracked::alive = 0;
    auto a = makeShared<Tracked>(7);
    EXPECT_EQ(a.useCount(), 1);

    {
        auto b = a;                            // copy -> +1
        EXPECT_EQ(a.useCount(), 2);
        auto c = a;                            // copy -> +1
        EXPECT_EQ(a.useCount(), 3);
        EXPECT_EQ(Tracked::alive, 1);          // still ONE object, shared
    }                                          // b, c die -> -2

    EXPECT_EQ(a.useCount(), 1);
    EXPECT_EQ(Tracked::alive, 1);
}

TEST(W19Smart, ObjectDiesWhenLastOwnerDoes) {
    Tracked::alive = 0;
    {
        auto a = makeShared<Tracked>(1);
        auto b = a;
        EXPECT_EQ(Tracked::alive, 1);
    }                                          // both die -> count 0 -> delete
    EXPECT_EQ(Tracked::alive, 0);
}

// ================= 3. weak_ptr: observes without owning
TEST(W19Smart, WeakPtrDoesNotKeepAlive) {
    WeakPtr<Tracked> w;
    {
        auto s = makeShared<Tracked>(1);
        w = s;                                 // weak observes
        EXPECT_EQ(s.useCount(), 1);            // weak did NOT bump strong
        EXPECT_FALSE(w.expired());
        EXPECT_TRUE(static_cast<bool>(w.lock()));   // lock succeeds: alive
    }                                          // s dies -> object destroyed
    EXPECT_TRUE(w.expired());                  // weak sees it's gone
    EXPECT_FALSE(static_cast<bool>(w.lock())); // lock returns empty
}

// ================= 4. THE CYCLE: two strong refs leak
// A deliberate leak must not turn the asan preset red, so instead of leaking for
// real we prove the leak SIGNATURE via use counts, then break the cycle to reclaim
// the memory. Telling a tool about an intentional leak is itself a Phase 10 skill.
TEST(W19Smart, StrongCycleLeaks) {
    Node::alive = 0;
    auto a = makeShared<Node>();
    auto b = makeShared<Node>();
    a->next        = b;                        // a -> b (strong)
    b->prev_strong = a;                        // b -> a (strong) : CYCLE

    // Each node is now owned TWICE: once by us, once by the other node.
    EXPECT_EQ(a.useCount(), 2);                // us + b->prev_strong
    EXPECT_EQ(b.useCount(), 2);                // us + a->next
    // If a and b left scope now, each count would drop to 1 (held by the other),
    // never reach 0, and both Nodes would leak. That is the cycle.

    b->prev_strong = SharedPtr<Node>{};        // break one edge -> counts can unwind
    EXPECT_EQ(a.useCount(), 1);                // now only we own a
    EXPECT_EQ(b.useCount(), 2);                // still us + a->next
}

// ================= 5. weak_ptr breaks the cycle
TEST(W19Smart, WeakBreaksTheCycle) {
    Node::alive = 0;
    {
        auto a = makeShared<Node>();
        auto b = makeShared<Node>();
        a->next      = b;                      // a -> b (strong)
        b->prev_weak = a;                      // b -> a (WEAK) : no cycle
        EXPECT_EQ(Node::alive, 2);
    }
    EXPECT_EQ(Node::alive, 0);                 // <-- both freed. No leak.
}
