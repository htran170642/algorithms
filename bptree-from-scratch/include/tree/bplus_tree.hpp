#pragma once
#include "common/config.hpp"
#include "common/types.hpp"
#include "page/page.hpp"
#include "page/btree_page.hpp"
#include "page/leaf_page.hpp"
#include "page/internal_page.hpp"
#include "tree/comparator.hpp"
#include "buffer/simple_page_manager.hpp"
#include <cassert>
#include <concepts>

namespace bptree {

template <typename PM>
concept PageManagerConcept = requires(PM& pm, page_id_t id, bool dirty) {
    { pm.NewPage(id)          } -> std::same_as<Page*>;
    { pm.FetchPage(id)        } -> std::same_as<Page*>;
    { pm.UnpinPage(id, dirty) } -> std::same_as<bool>;
};

template <typename Key, typename Value,
          typename Cmp = DefaultComparator<Key>,
          typename PM  = SimplePageManager>
    requires PageManagerConcept<PM>
class BPlusTree {
public:
    using LeafPage_     = LeafPage<Key, Value>;
    using InternalPage_ = InternalPage<Key>;

    explicit BPlusTree(PM& pm, Cmp cmp = {}) noexcept
        : pm_(pm), cmp_(std::move(cmp)) {}

    bool      IsEmpty()      const noexcept { return root_page_id_ == INVALID_PAGE_ID; }
    page_id_t GetRootPageId() const noexcept { return root_page_id_; }

    // Returns the pinned leaf page that should contain key.
    // Returns nullptr if the tree is empty.
    // Caller MUST call pm_.UnpinPage() when done.
    Page* FindLeafPage(const Key& key);

private:
    PM&       pm_;
    Cmp       cmp_;
    page_id_t root_page_id_ = INVALID_PAGE_ID;
};

// --- FindLeafPage ---

template <typename Key, typename Value, typename Cmp, typename PM>
    requires PageManagerConcept<PM>
Page* BPlusTree<Key, Value, Cmp, PM>::FindLeafPage(const Key& key) {
    if (root_page_id_ == INVALID_PAGE_ID) {
        return nullptr;
    }

    page_id_t current_id = root_page_id_;

    while (true) {
        Page* page      = pm_.FetchPage(current_id);
        auto* node      = page->As<BPlusTreePage>();

        if (node->IsLeaf()) {
            return page;  // caller must unpin
        }

        auto*     internal = page->As<InternalPage_>();
        page_id_t next_id  = internal->Lookup(key, cmp_);
        pm_.UnpinPage(current_id, false);
        current_id = next_id;
    }
}

} // namespace bptree
