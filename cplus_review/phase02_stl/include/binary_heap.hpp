#pragma once

// A binary max-heap living in a flat std::vector — no pointers.
// For a node at index i: left = 2i+1, right = 2i+2, parent = (i-1)/2.
// Contiguous storage makes it cache-friendly (W13 again). This is what
// std::priority_queue wraps.
//
// Invariant: every parent >= its children. So the max is always at index 0.

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace cr {

template <typename T, typename Compare = std::less<T>>
class BinaryHeap {
public:
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }

    [[nodiscard]] const T& top() const { return data_.front(); }   // O(1) — max at root

    void push(const T& value) {
        data_.push_back(value);          // add at the end...
        siftUp(data_.size() - 1);        // ...then bubble it up to its place
    }

    void pop() {
        data_.front() = std::move(data_.back());   // last element to the root
        data_.pop_back();
        if (!data_.empty()) {
            siftDown(0);                 // sink the root down to its place
        }
    }

private:
    static std::size_t parent(std::size_t i) { return (i - 1) / 2; }
    static std::size_t left(std::size_t i)   { return 2 * i + 1; }
    static std::size_t right(std::size_t i)  { return 2 * i + 2; }

    // Move element i up while it's bigger than its parent.
    void siftUp(std::size_t i) {
        while (i > 0 && comp_(data_[parent(i)], data_[i])) {   // parent < child -> swap
            std::swap(data_[i], data_[parent(i)]);
            i = parent(i);
        }
    }

    // Move element i down while a child is bigger than it.
    void siftDown(std::size_t i) {
        const std::size_t n = data_.size();
        while (true) {
            std::size_t largest = i;
            std::size_t l = left(i);
            std::size_t r = right(i);

            if (l < n && comp_(data_[largest], data_[l])) largest = l;
            if (r < n && comp_(data_[largest], data_[r])) largest = r;

            if (largest == i) break;             // heap property restored
            std::swap(data_[i], data_[largest]);
            i = largest;
        }
    }

    std::vector<T> data_;
    Compare        comp_{};
};

}  // namespace cr
