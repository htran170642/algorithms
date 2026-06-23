#pragma once

#include "page/btree_page.hpp"
#include "common/config.hpp"
#include <algorithm>
#include <utility>

namespace bptree {

template <typename Key>
class InternalPage : public BPlusTreePage {
public:
    using MappingType = std::pair<Key, page_id_t>;

    static constexpr uint32_t MAX_SIZE =
        (PAGE_SIZE - sizeof(BPlusTreePage)) / sizeof(MappingType);


    static void Init(InternalPage* self, page_id_t page_id, page_id_t parent_page_id) noexcept {
        self->BPlusTreePage::Init(page_id, parent_page_id, false, MAX_SIZE);
    }

    // access key and value at index i
    Key KeyAt(uint32_t index) const noexcept {
        return array_[index].first;
    }
    page_id_t ValueAt(uint32_t index) const noexcept {
        return array_[index].second;
    }
    void SetKeyAt(uint32_t index, const Key& key) noexcept {
        array_[index].first = key;
    }
    void SetValueAt(uint32_t index, page_id_t child) noexcept {
        array_[index].second = child;
    }

    // Find the child page that should contain the given key.
    // Binary search over array_[1..size_-1] (skip array_[0].key — unused).
    // Returns array_[i].child where i is the largest index with array_[i].key <= key.
    template <typename Cmp>
    page_id_t Lookup(const Key& key, const Cmp& cmp) const noexcept {
        uint32_t lo = 1, hi = GetSize();
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            if (cmp(array_[mid].first, key) <= 0) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return array_[lo - 1].second;
    }

    // Insert a new separator key and right child immediately after old_child.
    // Called after a child page splits: old_child is the left half,
    // new_child is the right half, key is the separator pushed up.
    void InsertNodeAfter(page_id_t old_child, const Key& key,
                         page_id_t new_child) noexcept {
        // Find the index of old_child in array_
        uint32_t idx = 0;
        while (idx < GetSize() && array_[idx].second != old_child) {
            ++idx;
        }
        // Shift everything after idx one position to the right
        std::move_backward(array_ + idx + 1, array_ + GetSize(),
                           array_ + GetSize() + 1);
        // Insert new separator key and right child at idx+1
        array_[idx + 1] = {key, new_child};
        IncreaseSize(1);
    }
    
    // Remove the entry containing child_id from array_.
    // Called after two child pages merge — the right child disappears.
    void RemoveChild(page_id_t child_id) noexcept {
        uint32_t idx = 0;
        while (idx < GetSize() && array_[idx].second != child_id) {
            ++idx;
        }
        // Shift everything after idx one position to the left
        std::move(array_ + idx + 1, array_ + GetSize(), array_ + idx);
        IncreaseSize(-1);
    }

    // Move the upper half of entries to recipient.
    // The middle key is "pushed up" to the parent — it is removed from this node
    // and returned via push_up_key (unlike LeafPage where the key stays).
    void MoveHalfTo(InternalPage* recipient, Key& push_up_key) noexcept {
        uint32_t half = GetSize() / 2;

        // The middle entry's key floats up to the parent
        push_up_key = array_[half].first;

        // recipient's leftmost child = the middle entry's child
        recipient->array_[0].second = array_[half].second;

        // Copy the upper half (after middle) into recipient
        uint32_t move_count = GetSize() - half - 1;
        std::copy(array_ + half + 1, array_ + GetSize(),
                  recipient->array_ + 1);
        recipient->SetSize(move_count + 1); // +1 for array_[0]
        SetSize(half);
    }

    // Move all entries into recipient, pulling parent_key down as new separator.
    // Called during merge: recipient is the left sibling absorbing this node.
    void MoveAllTo(InternalPage* recipient, const Key& parent_key) noexcept {
        uint32_t base = recipient->GetSize();

        // Pull the parent separator key down into recipient
        recipient->array_[base].first  = parent_key;
        recipient->array_[base].second = array_[0].second;

        // Copy remaining entries
        std::copy(array_ + 1, array_ + GetSize(),
                  recipient->array_ + base + 1);
        recipient->IncreaseSize(GetSize());
        SetSize(0);
    }
private:
    MappingType array_[MAX_SIZE];

};

static_assert(sizeof(InternalPage<int32_t>) <= PAGE_SIZE,
    "InternalPage must fit within a single page");
} // namespace bptree