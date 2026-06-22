#pragma once 

#include <compare>
#include <string>

namespace bptree {

template <typename Key>
struct DefaultComparator {
    int operator() (const Key& a, const Key& b) const noexcept {
        if (a < b) return -1;
        if (a > b) return 1;
        return 0;
    }
};

template <>
struct DefaultComparator<std::string> {
    int operator() (const std::string& a, const std::string& b) const noexcept {
        return a.compare(b);
    }
};

} // namespace bptree