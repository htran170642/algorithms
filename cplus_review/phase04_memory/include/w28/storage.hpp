// W28 — manual object lifetime, decoupled from storage.
//
// Storage<T> is the smallest honest example of what std::optional and every STL
// container do inside: it owns RAW bytes (never a live T), and constructs /
// destroys the T on demand via placement new + an explicit destructor call.
//
// The whole lesson is that these are TWO separate responsibilities:
//   - storage:  the aligned byte buffer. Owned here, freed here (it's a member).
//   - object:   the T living in that buffer. construct() builds it, destroy()
//               ends it. Neither touches the storage.
//
// Throwaway teaching type — you use std::optional / the containers for real.

#ifndef W28_STORAGE_HPP
#define W28_STORAGE_HPP

#include <cstddef>
#include <new>       // std::launder, placement new
#include <utility>   // std::forward

namespace w28 {

template <typename T>
class Storage {
public:
    Storage() noexcept = default;

    // Non-copyable: copying raw storage would bit-blit a live T behind its own
    // back (no copy ctor call) — exactly the kind of UB this type exists to
    // teach against. A real optional implements copy carefully; we opt out.
    Storage(const Storage&)            = delete;
    Storage& operator=(const Storage&) = delete;

    // RAII: if a T is still living when Storage dies, end it. This is the line
    // that turns "manual lifetime" back into "automatic" — forget it and the T
    // leaks its resources even though the byte buffer itself is reclaimed.
    ~Storage() {
        if (engaged_) {
            destroy();
        }
    }

    // Build a T in-place. No allocation happens — buf_ already exists. Returns a
    // reference to the freshly-live object. Precondition: not already engaged.
    template <typename... Args>
    T& construct(Args&&... args) {
        T* p = ::new (static_cast<void*>(buf_)) T(std::forward<Args>(args)...);
        engaged_ = true;
        return *p;
    }

    // End the object's lifetime — runs ~T(), leaves buf_ as raw bytes again.
    // Does NOT free buf_ (it's a member; it outlives the object). Precondition:
    // engaged.
    void destroy() noexcept {
        get().~T();
        engaged_ = false;
    }

    // Access the live object. std::launder is the load-bearing detail: we build
    // the T through a std::byte* but read it back through a T*, and after a
    // re-construct the compiler may not assume the new object shares the old
    // one's (possibly const/reference) members. launder says "this pointer names
    // whatever object currently lives here — re-read it". Precondition: engaged.
    T&       get()       noexcept { return *std::launder(reinterpret_cast<T*>(buf_)); }
    const T& get() const noexcept { return *std::launder(reinterpret_cast<const T*>(buf_)); }

    [[nodiscard]] bool engaged() const noexcept { return engaged_; }

private:
    // alignas(T): buf_ must be aligned for T, not merely sized for it. Without
    // this, placement-newing a T with alignof > 1 into a char/byte array is UB.
    alignas(T) std::byte buf_[sizeof(T)];
    bool engaged_ = false;
};

}  // namespace w28

#endif  // W28_STORAGE_HPP
