// W30 — a fixed-size object pool with a free-list threaded through dead slots.
//
// Where the arena (W29) refuses to free individual objects, a pool is built for
// exactly that: many objects of ONE type, created and destroyed on independent
// schedules. Every allocate/deallocate is O(1) — no search, no per-block
// metadata, no lock — because a freed slot stores the "next free" pointer INSIDE
// itself (the object is dead, so its bytes are free real estate).
//
// This is the shape of std::pmr::unsynchronized_pool_resource; we build it once
// to see the free-list, then reach for the standard one in real code.
//
// Limitation (deliberate, same spirit as the arena): the pool does NOT track
// which slots still hold live objects, so ~ObjectPool does NOT run ~T() on
// leftovers. Destroy everything you create before the pool dies.

#ifndef W30_POOL_HPP
#define W30_POOL_HPP

#include <cstddef>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace w30 {

template <typename T>
class ObjectPool {
public:
    explicit ObjectPool(std::size_t blocks_per_chunk = 64)
        : blocks_per_chunk_(blocks_per_chunk == 0 ? 1 : blocks_per_chunk) {}

    ObjectPool(const ObjectPool&)            = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    ~ObjectPool() = default;  // chunks_ (unique_ptr) free the storage; see note above

    // Construct a T in a recycled (or fresh) slot. O(1): pop the free-list, then
    // placement-new in place — the same construct-into-raw-storage as W28.
    template <typename... Args>
    T* create(Args&&... args) {
        Slot* s = pop_free();
        return ::new (static_cast<void*>(s)) T(std::forward<Args>(args)...);
    }

    // End the object's life and return its slot to the free-list for reuse. The
    // slot is NOT returned to the OS — that is the whole point. O(1).
    void destroy(T* p) noexcept {
        p->~T();
        push_free(reinterpret_cast<Slot*>(p));
    }

private:
    // A slot is either raw storage for a live T, or a free-list link when dead.
    // The union guarantees it is big enough and aligned for BOTH — so a freed
    // slot can host the next-pointer with zero extra space.
    union Slot {
        alignas(T) std::byte storage[sizeof(T)];
        Slot* next;
    };

    Slot* pop_free() {
        if (free_head_ == nullptr) {
            grow();
        }
        Slot* s   = free_head_;
        free_head_ = s->next;
        return s;
    }

    void push_free(Slot* s) noexcept {
        s->next    = free_head_;   // LIFO: last freed is first reused (cache-hot)
        free_head_ = s;
    }

    // Reserve one more chunk of slots and thread them all onto the free-list.
    // Chunks are never freed individually; the vector owns them for the pool's
    // lifetime, so pointers handed out earlier stay valid.
    void grow() {
        auto chunk = std::make_unique<Slot[]>(blocks_per_chunk_);
        for (std::size_t i = 0; i < blocks_per_chunk_; ++i) {
            chunk[i].next = free_head_;
            free_head_    = &chunk[i];
        }
        chunks_.push_back(std::move(chunk));
    }

    std::size_t                            blocks_per_chunk_;
    Slot*                                  free_head_ = nullptr;
    std::vector<std::unique_ptr<Slot[]>>   chunks_;
};

}  // namespace w30

#endif  // W30_POOL_HPP
