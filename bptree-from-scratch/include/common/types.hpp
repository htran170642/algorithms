#pragma once

#include <cstdint>
#include <limits>

namespace bptree{
using page_id_t = uint32_t;
using frame_id_t = uint32_t;

constexpr page_id_t INVALID_PAGE_ID = std::numeric_limits<page_id_t>::max();
constexpr frame_id_t INVALID_FRAME_ID = std::numeric_limits<frame_id_t>::max();
}
