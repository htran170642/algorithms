#pragma once

#include "common/types.hpp"
#include <cstddef>

namespace bptree {
constexpr std::size_t PAGE_SIZE = 4096;
constexpr std::size_t DEFAULT_POOL_SIZE = 64;
} // namespace bptree