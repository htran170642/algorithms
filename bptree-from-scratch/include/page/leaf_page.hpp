#pragma once

#include "page/btree_page.hpp"
#include "common/config.hpp"
#include <algorithm>
#include <optional>
#include <utility>

namespace bptree {

template <typename Key, typename Value>
class LeafPage : public BPlusTreePage {
public:
    using KVPair = std::pair<Key, Value>;

    static constexpr uint32_t MAX_SIZE =
        (PAGE_SIZE - sizeof(BPlusTreePage) - sizeof(page_id_t))
        / sizeof(KVPair);

    static void Init(LeafPage* self, page_id_t page_id, page_id_t parent_page_id) noexcept {
        self->BPlusTreePage::Init(page_id, parent_page_id, true, MAX_SIZE);
        self->next_page_id_ = INVALID_PAGE_ID;
    }

    page_id_t GetNextPageId() const noexcept { return next_page_id_; }
    void SetNextPageId(page_id_t next_page_id) noexcept { next_page_id_ = next_page_id; }

    template <typename Cmp>
    std::optional<Value> Lookup(const Key& key, const Cmp& cmp) const noexcept {
        auto it = std::lower_bound(pairs_, pairs_ + GetSize(), key,
            [&](const KVPair& pair, const Key& k) {
                return cmp(pair.first, k) < 0;
            });
        if (it != pairs_ + GetSize() && cmp(it->first, key) == 0) {
            return it->second;
        }
        return std::nullopt;
    }

    template <typename Cmp>
    bool Insert(const Key& key, const Value& value, const Cmp& cmp) noexcept {
        auto it = std::lower_bound(pairs_, pairs_ + GetSize(), key,
            [&](const KVPair& pair, const Key& k) {
                return cmp(pair.first, k) < 0;
            });
        
        // Check for duplicates
        if (it != pairs_ + GetSize() && cmp(it->first, key) == 0) {
            return false;
        }

        // Shift elements to the right to make space for the new key-value pair
        std::move_backward(it, pairs_ + GetSize(), pairs_ + GetSize() + 1);
        *it = {key, value};
        IncreaseSize(1);
        return true;
    }


    template <typename Cmp>
    bool Remove(const Key& key, const Cmp& cmp) noexcept {
        auto it = std::lower_bound(pairs_, pairs_ + GetSize(), key,
            [&](const KVPair& pair, const Key& k) {
                return cmp(pair.first, k) < 0;
            });
        
            if (it == pairs_ + GetSize() || cmp(it->first, key) != 0) {
            return false; // not found
        }

        // Shift elements to the left to fill the gap
        std::move(it + 1, pairs_ + GetSize(), it);
        IncreaseSize(-1);
        return true;
    }

    void MoveHalfTo(LeafPage* recipient) noexcept {
        uint32_t half = GetSize() / 2;
        uint32_t move_count = GetSize() - half;
        
        std::copy(pairs_ + half, pairs_ + GetSize(), recipient->pairs_);
        recipient->IncreaseSize(move_count);
        recipient->SetNextPageId(GetNextPageId());
        recipient->SetPageId(recipient->GetPageId());
        SetNextPageId(recipient->GetPageId());
        SetSize(half);
    }

    void MoveAllTo(LeafPage* recipient) noexcept {
        std::copy(pairs_, pairs_ + GetSize(), recipient->pairs_ + recipient->GetSize());
        recipient->IncreaseSize(GetSize());
        recipient->SetNextPageId(GetNextPageId());
        SetSize(0);
    }

    KVPair& EntryAt(uint32_t idx) noexcept { return pairs_[idx]; }
    const KVPair& EntryAt(uint32_t idx) const noexcept { return pairs_[idx]; }


private:
    page_id_t next_page_id_ = INVALID_PAGE_ID;
    KVPair pairs_[MAX_SIZE];
};

static_assert(sizeof(LeafPage<int32_t, int32_t>) <= PAGE_SIZE,
    "LeafPage must fit within a single page");
static_assert(std::is_trivially_copyable_v<int32_t>);


} // namespace bptree