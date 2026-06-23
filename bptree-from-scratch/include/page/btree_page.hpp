#pragma once

#include "common/config.hpp"
#include "common/types.hpp"
#include <cstdint>
#include <cstddef>

namespace bptree {

class BPlusTreePage {
public:
    void Init(page_id_t page_id, page_id_t parent_page_id,
              bool is_leaf, uint32_t max_size) noexcept {
        page_id_ = page_id;
        parent_page_id_ = parent_page_id;
        is_leaf_ = is_leaf;
        max_size_ = max_size;
        size_ = 0;
    }

    page_id_t GetPageId()       const noexcept { return page_id_; }
    page_id_t GetParentPageId() const noexcept { return parent_page_id_; }
    uint32_t  GetSize()         const noexcept { return size_; }
    uint32_t  GetMaxSize()      const noexcept { return max_size_; }
    bool      IsLeaf()          const noexcept { return is_leaf_; }

    void SetPageId(page_id_t page_id) noexcept { page_id_ = page_id; }
    void SetParentPageId(page_id_t parent_page_id) noexcept { parent_page_id_ = parent_page_id; }
    void SetSize(uint32_t size) noexcept { size_ = size; }
    void IncreaseSize(int amount = 1) noexcept { size_ += amount; }

    bool IsFull() const noexcept { return size_ >= max_size_; }
    bool IsUnderflow() const noexcept { return size_ < (max_size_ / 2); }
    bool IsRootPage() const noexcept { return parent_page_id_ == INVALID_PAGE_ID; }
private:
    page_id_t page_id_ = INVALID_PAGE_ID;
    page_id_t parent_page_id_ = INVALID_PAGE_ID;
    uint32_t size_ = 0;
    uint32_t max_size_ = 0;
    bool is_leaf_ = false;
    uint8_t pad_[3] = {};
};

static_assert(sizeof(BPlusTreePage) == 20,
    "BPlusTreePage header must be exactly 20 bytes");


} // namespace bptree