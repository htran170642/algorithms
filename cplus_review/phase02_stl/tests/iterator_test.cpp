#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <ranges>
#include <vector>

namespace {

// A minimal random-access iterator over a plain int array.
// If std::sort and std::ranges accept THIS, you've understood the iterator
// protocol: algorithms don't care what you are, only that you behave like one.
class IntRange {
public:
    class Iterator {
    public:
        // These five typedefs are the "iterator contract" the STL reads.
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = int;
        using difference_type   = std::ptrdiff_t;
        using pointer           = int*;
        using reference         = int&;

        Iterator() = default;
        explicit Iterator(int* p) : p_(p) {}

        reference operator*() const { return *p_; }
        pointer   operator->() const { return p_; }

        Iterator& operator++()    { ++p_; return *this; }         // pre
        Iterator  operator++(int) { auto t = *this; ++p_; return t; }  // post
        Iterator& operator--()    { --p_; return *this; }
        Iterator  operator--(int) { auto t = *this; --p_; return t; }

        // random-access requirements — the ones list can't provide
        Iterator& operator+=(difference_type n) { p_ += n; return *this; }
        Iterator& operator-=(difference_type n) { p_ -= n; return *this; }
        Iterator  operator+(difference_type n) const { return Iterator(p_ + n); }
        Iterator  operator-(difference_type n) const { return Iterator(p_ - n); }
        difference_type operator-(const Iterator& o) const { return p_ - o.p_; }
        reference operator[](difference_type n) const { return p_[n]; }

        auto operator<=>(const Iterator&) const = default;   // C++20: all comparisons at once
        bool operator==(const Iterator&) const = default;

    private:
        int* p_ = nullptr;
    };

    IntRange(int* data, std::size_t n) : begin_(data), end_(data + n) {}

    Iterator begin() const { return Iterator(begin_); }
    Iterator end()   const { return Iterator(end_); }

private:
    int* begin_;
    int* end_;
};

}  // namespace

// ================= 1. std::sort works on OUR iterator (it's random-access)
TEST(W11Iterator, StdSortAcceptsOurCustomIterator) {
    int raw[] = {5, 2, 8, 1, 9, 3};
    IntRange r(raw, 6);

    std::sort(r.begin(), r.end());     // the SAME std::sort that rejects list

    EXPECT_TRUE(std::is_sorted(raw, raw + 6));
    EXPECT_EQ(raw[0], 1);
    EXPECT_EQ(raw[5], 9);
}

// ================= 2. algorithms over manual loops
TEST(W11Iterator, AlgorithmsExpressIntent) {
    std::vector<int> v = {3, 12, 5, 20, 8, 15};

    auto big = std::count_if(v.begin(), v.end(), [](int x){ return x > 10; });
    EXPECT_EQ(big, 3);

    auto total = std::accumulate(v.begin(), v.end(), 0);
    EXPECT_EQ(total, 63);

    auto max_it = std::max_element(v.begin(), v.end());
    EXPECT_EQ(*max_it, 20);
}

// ================= 3. C++20 ranges: sort a whole container, no begin/end
TEST(W11Iterator, RangesSortIsCleaner) {
    std::vector<int> v = {5, 2, 8, 1};
    std::ranges::sort(v);
    EXPECT_EQ(v, (std::vector<int>{1, 2, 5, 8}));
}

// ================= 4. views: lazy pipeline, no intermediate copies
TEST(W11Iterator, ViewsAreLazyAndComposable) {
    std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8};

    // "even numbers, squared" — nothing computes until we iterate
    auto pipeline = v | std::views::filter([](int x){ return x % 2 == 0; })
                      | std::views::transform([](int x){ return x * x; });

    std::vector<int> result(pipeline.begin(), pipeline.end());
    EXPECT_EQ(result, (std::vector<int>{4, 16, 36, 64}));   // 2²,4²,6²,8²
}
