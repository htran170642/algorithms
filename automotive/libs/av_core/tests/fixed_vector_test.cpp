// W1 — FixedVector: the no-heap container.
//
// The tests assert OBSERVABLE properties, not implementation details:
//   - elements live inside the object (the no-heap claim, proven structurally)
//   - every constructed element is destroyed exactly once
//   - move really is O(n) (the trade-off is measured, not asserted in a comment)
//   - a throwing copy leaves nothing behind (exception safety + ASan leak check)
//   - a non-default-constructible T works (proves the raw-storage choice)

#include "av/fixed_vector.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {

/// Counts its own lifecycle. Deliberately has NO default constructor: an
/// implementation using `T data_[Capacity]` could not hold this type at all.
struct Tracker {
  static int ctors;
  static int copies;
  static int moves;
  static int dtors;

  int value;

  explicit Tracker(int v) : value(v) { ++ctors; }
  Tracker(const Tracker& o) : value(o.value) { ++copies; }
  Tracker(Tracker&& o) noexcept : value(o.value) { ++moves; }
  Tracker& operator=(const Tracker&) = default;
  Tracker& operator=(Tracker&&) noexcept = default;
  ~Tracker() { ++dtors; }

  static void reset() { ctors = copies = moves = dtors = 0; }
  static int live() { return ctors + copies + moves - dtors; }
};

int Tracker::ctors = 0;
int Tracker::copies = 0;
int Tracker::moves = 0;
int Tracker::dtors = 0;

/// Throws on the Nth copy, so partial-construction rollback can be observed.
struct Bomb {
  static int live;
  static int copies;
  static int throw_on_copy;  // -1 = never

  int value;

  explicit Bomb(int v) : value(v) { ++live; }
  Bomb(const Bomb& o) : value(o.value) {
    ++copies;
    if (copies == throw_on_copy) { throw std::runtime_error("boom"); }
    ++live;  // only a FULLY constructed Bomb counts as live
  }
  ~Bomb() { --live; }

  static void reset() {
    live = 0;
    copies = 0;
    throw_on_copy = -1;
  }
};

int Bomb::live = 0;
int Bomb::copies = 0;
int Bomb::throw_on_copy = -1;

}  // namespace

// --- the headline claim: no heap ---------------------------------------------

// Structural proof #1: the object is at least as big as its payload, so the
// storage cannot be living behind a pointer.
static_assert(sizeof(av::FixedVector<int, 8>) >= 8U * sizeof(int),
              "FixedVector must carry its storage inline");

TEST(FixedVector, ElementsLiveInsideTheObject) {
  av::FixedVector<int, 8> v;
  ASSERT_TRUE(v.try_push_back(42));
  ASSERT_TRUE(v.try_push_back(43));

  // Proof #2, at runtime: &v[0] lies within [&v, &v + sizeof(v)).
  // A heap-backed container would put it somewhere else entirely.
  const auto self = reinterpret_cast<std::uintptr_t>(&v);
  const auto first = reinterpret_cast<std::uintptr_t>(&v[0]);
  const auto last = reinterpret_cast<std::uintptr_t>(&v[1]);

  EXPECT_GE(first, self);
  EXPECT_LT(last, self + sizeof(v));
}

// --- basic behaviour ----------------------------------------------------------

TEST(FixedVector, StartsEmptyAndTracksSize) {
  av::FixedVector<int, 4> v;
  EXPECT_TRUE(v.empty());
  EXPECT_FALSE(v.full());
  EXPECT_EQ(v.size(), 0U);
  // The preprocessor runs BEFORE the parser and does not understand templates:
  // it sees the comma in FixedVector<int, 4> as a macro argument separator.
  // A using-alias sidesteps it more readably than an extra pair of parentheses.
  using Vec4 = av::FixedVector<int, 4>;
  EXPECT_EQ(Vec4::capacity(), 4U);

  ASSERT_TRUE(v.try_push_back(1));
  EXPECT_EQ(v.size(), 1U);
  EXPECT_EQ(v.front(), 1);
  EXPECT_EQ(v.back(), 1);
}

TEST(FixedVector, TryPushBackReportsFullInsteadOfThrowing) {
  av::FixedVector<int, 2> v;
  EXPECT_TRUE(v.try_push_back(1));
  EXPECT_TRUE(v.try_push_back(2));
  EXPECT_TRUE(v.full());

  // The automotive path: overflow is a VALUE the caller must handle, not an
  // exception and not UB.
  EXPECT_FALSE(v.try_push_back(3));
  EXPECT_EQ(v.size(), 2U);
}

TEST(FixedVector, PushBackThrowsWhenFull) {
  av::FixedVector<int, 1> v;
  v.push_back(1);
  EXPECT_THROW(v.push_back(2), std::length_error);
  EXPECT_EQ(v.size(), 1U);
}

TEST(FixedVector, AtIsCheckedAndInitializerListRespectsCapacity) {
  av::FixedVector<int, 4> v{10, 20, 30};
  EXPECT_EQ(v.size(), 3U);
  EXPECT_EQ(v.at(2), 30);
  EXPECT_THROW(static_cast<void>(v.at(3)), std::out_of_range);

  using Small = av::FixedVector<int, 2>;
  EXPECT_THROW(Small({1, 2, 3}), std::length_error);
}

TEST(FixedVector, IteratesInInsertionOrder) {
  av::FixedVector<int, 4> v{1, 2, 3};
  int sum = 0;
  for (const int x : v) { sum += x; }
  EXPECT_EQ(sum, 6);
  EXPECT_EQ(v.end() - v.begin(), 3);
}

// --- lifetime -----------------------------------------------------------------

TEST(FixedVector, DestroysEveryElementExactlyOnce) {
  Tracker::reset();
  {
    av::FixedVector<Tracker, 4> v;
    ASSERT_NE(v.try_emplace_back(1), nullptr);
    ASSERT_NE(v.try_emplace_back(2), nullptr);
    ASSERT_NE(v.try_emplace_back(3), nullptr);
    EXPECT_EQ(Tracker::live(), 3);
  }
  EXPECT_EQ(Tracker::live(), 0) << "destructor must destroy exactly what was built";
}

TEST(FixedVector, PopBackAndClearDestroyEagerly) {
  Tracker::reset();
  av::FixedVector<Tracker, 4> v;
  ASSERT_NE(v.try_emplace_back(1), nullptr);
  ASSERT_NE(v.try_emplace_back(2), nullptr);

  v.pop_back();
  EXPECT_EQ(Tracker::live(), 1);

  v.clear();
  EXPECT_EQ(Tracker::live(), 0);
  EXPECT_TRUE(v.empty());
}

// --- the trade-off, measured --------------------------------------------------

TEST(FixedVector, MoveIsLinearNotConstant) {
  Tracker::reset();
  av::FixedVector<Tracker, 8> src;
  ASSERT_NE(src.try_emplace_back(1), nullptr);
  ASSERT_NE(src.try_emplace_back(2), nullptr);
  ASSERT_NE(src.try_emplace_back(3), nullptr);
  EXPECT_EQ(Tracker::moves, 0);

  av::FixedVector<Tracker, 8> dst(std::move(src));

  // THE price of inline storage. std::vector would move ZERO elements here — it
  // just steals three pointers. FixedVector has no pointer to steal, so the cost
  // is O(n). This is why you never pass one by value.
  EXPECT_EQ(Tracker::moves, 3);
  EXPECT_EQ(dst.size(), 3U);
  EXPECT_TRUE(src.empty());
  EXPECT_EQ(Tracker::live(), 3);
}

TEST(FixedVector, HoldsMoveOnlyTypes) {
  av::FixedVector<std::unique_ptr<int>, 4> v;
  ASSERT_TRUE(v.try_push_back(std::make_unique<int>(7)));
  EXPECT_EQ(*v[0], 7);

  av::FixedVector<std::unique_ptr<int>, 4> moved(std::move(v));
  EXPECT_EQ(moved.size(), 1U);
  EXPECT_EQ(*moved[0], 7);
}

// --- exception safety ---------------------------------------------------------

TEST(FixedVector, ThrowingCopyLeavesNothingBehind) {
  Bomb::reset();
  av::FixedVector<Bomb, 8> src;
  for (int i = 0; i < 4; ++i) { ASSERT_NE(src.try_emplace_back(i), nullptr); }
  ASSERT_EQ(Bomb::live, 4);

  Bomb::copies = 0;
  Bomb::throw_on_copy = 3;  // two elements get built, then the third explodes

  using BombVec = av::FixedVector<Bomb, 8>;  // comma would break the macro
  EXPECT_THROW(BombVec dst(src), std::runtime_error);

  // The half-built vector never runs its own destructor — it was still under
  // construction — so copy_from must roll back by hand. If it did not, these two
  // Bombs would leak, and ASan would say so too.
  EXPECT_EQ(Bomb::live, 4) << "partially constructed elements must be destroyed";
}
