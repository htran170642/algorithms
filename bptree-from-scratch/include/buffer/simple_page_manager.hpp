#pragma once

#include "common/config.hpp"
#include "common/types.hpp"
#include "page/page.hpp"
#include <memory>
#include <vector>
#include <cassert>

namespace bptree {

// In-memory page manager for testing tree logic without disk I/O.
// Pages are heap-allocated via unique_ptr so their addresses are stable
// even as the vector grows. No eviction, no disk.
class SimplePageManager {
public:
    explicit SimplePageManager(uint32_t capacity) {
        pages_.reserve(capacity);
    }

    // Allocate a new zeroed page. Returns pinned page pointer and its id.
    Page* NewPage(page_id_t& out_id) {
        assert(pages_.size() < pages_.capacity());
        out_id = static_cast<page_id_t>(pages_.size());
        auto& ptr = pages_.emplace_back(std::make_unique<Page>());
        ptr->Reset();
        ptr->SetPageId(out_id);
        return ptr.get();
    }

    // Fetch an existing page by id.
    Page* FetchPage(page_id_t id) {
        assert(id < pages_.size());
        return pages_[id].get();
    }

    // No-op: all pages always live in memory.
    bool UnpinPage(page_id_t /*id*/, bool /*is_dirty*/) noexcept {
        return true;
    }

    bool FlushPage(page_id_t /*id*/) noexcept { return true; }
    void FlushAll()                  noexcept {}

    uint32_t PageCount() const noexcept {
        return static_cast<uint32_t>(pages_.size());
    }

private:
    std::vector<std::unique_ptr<Page>> pages_;
};

} // namespace bptree
