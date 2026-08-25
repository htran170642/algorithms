#ifndef AV_CORE_FIXED_VECTOR_HPP
#define AV_CORE_FIXED_VECTOR_HPP

#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace av {

/// A sequence container whose capacity is part of its TYPE, so it never touches
/// the heap. Elements live inside the object itself.
///
/// WHY (notes/w01_no_heap.md):
///   1. malloc's WCET is unbounded — it may walk free lists, take a lock, or
///      syscall into mmap. A hard real-time path cannot be analysed with it.
///   2. Fragmentation over a 15-year service life. There is no "restart the
///      service at 3am" in a vehicle.
///   3. Out-of-memory has no good answer: bad_alloc needs exceptions (which
///      AUTOSAR C++14 restricts), and abort() is not a safe state at 100 km/h.
///   4. ISO 26262 wants a PROVEN memory bound. Static storage is provable; heap
///      usage is not.
///   5. Freedom from interference — a QM component sharing a heap with an ASIL-D
///      component can starve it. Separate static storage keeps them independent.
///
/// WHEN NOT TO USE IT:
///   - Capacity is in the type: FixedVector<int,8> and FixedVector<int,16> are
///     unrelated types. One function taking both needs a template or a span.
///   - sizeof is always Capacity*sizeof(T), full or empty. 100 objects of
///     Capacity 1000 burn that RAM regardless of use. The heap allocates lazily.
///   - Move is O(n), NOT O(1). std::vector's move swaps three pointers; this must
///     move every element, because the storage is INSIDE the object and there is
///     no pointer to steal. Do not pass by value.
template <typename T, std::size_t Capacity>
class FixedVector {
  static_assert(Capacity > 0U, "FixedVector<T, 0> cannot hold anything");

 public:
  using value_type      = T;
  using size_type       = std::size_t;
  using reference       = T&;
  using const_reference = const T&;
  using pointer         = T*;
  using const_pointer   = const T*;
  using iterator        = T*;
  using const_iterator  = const T*;

  FixedVector() noexcept = default;

  FixedVector(std::initializer_list<T> init) {
    if (init.size() > Capacity) {
      throw std::length_error("FixedVector: initializer_list exceeds Capacity");
    }
    copy_from(init.begin(), init.end());
  }

  FixedVector(const FixedVector& other) { copy_from(other.begin(), other.end()); }

  /// O(n), unavoidably — see the class comment. Elements live inline, so there is
  /// no buffer pointer to hand over.
  FixedVector(FixedVector&& other) noexcept(std::is_nothrow_move_constructible<T>::value) {
    move_from(other);
  }

  FixedVector& operator=(const FixedVector& other) {
    if (this != &other) {
      clear();
      copy_from(other.begin(), other.end());
    }
    return *this;
  }

  FixedVector& operator=(FixedVector&& other) noexcept(
      std::is_nothrow_move_constructible<T>::value) {
    if (this != &other) {
      clear();
      move_from(other);
    }
    return *this;
  }

  ~FixedVector() { clear(); }

  // --- capacity ---------------------------------------------------------------
  [[nodiscard]] size_type size() const noexcept { return size_; }
  [[nodiscard]] static constexpr size_type capacity() noexcept { return Capacity; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0U; }
  [[nodiscard]] bool full() const noexcept { return size_ == Capacity; }

  // --- element access ---------------------------------------------------------
  // operator[] carries a PRECONDITION (asserted in debug, absent in release);
  // at() carries a CHECK. Automotive code uses at() at trust boundaries and
  // operator[] in hot loops it has already bounds-proven.
  reference operator[](size_type i) noexcept {
    assert(i < size_);
    return data()[i];
  }
  const_reference operator[](size_type i) const noexcept {
    assert(i < size_);
    return data()[i];
  }

  reference at(size_type i) {
    if (i >= size_) { throw std::out_of_range("FixedVector::at"); }
    return data()[i];
  }
  const_reference at(size_type i) const {
    if (i >= size_) { throw std::out_of_range("FixedVector::at"); }
    return data()[i];
  }

  reference       front() noexcept       { assert(!empty()); return data()[0]; }
  const_reference front() const noexcept { assert(!empty()); return data()[0]; }
  reference       back() noexcept        { assert(!empty()); return data()[size_ - 1U]; }
  const_reference back() const noexcept  { assert(!empty()); return data()[size_ - 1U]; }

  // --- iterators --------------------------------------------------------------
  iterator       begin() noexcept        { return data(); }
  const_iterator begin() const noexcept  { return data(); }
  const_iterator cbegin() const noexcept { return data(); }
  iterator       end() noexcept          { return data() + size_; }
  const_iterator end() const noexcept    { return data() + size_; }
  const_iterator cend() const noexcept   { return data() + size_; }

  // --- modifiers --------------------------------------------------------------
  // The no-exception path. Returns false when full; [[nodiscard]] makes ignoring
  // that impossible by accident. This is the API automotive code reaches for.
  [[nodiscard]] bool try_push_back(const T& v) {
    if (full()) { return false; }
    construct_at_end(v);
    return true;
  }
  [[nodiscard]] bool try_push_back(T&& v) {
    if (full()) { return false; }
    construct_at_end(std::move(v));
    return true;
  }
  /// Returns nullptr when full, otherwise a pointer to the new element.
  template <typename... Args>
  [[nodiscard]] T* try_emplace_back(Args&&... args) {
    if (full()) { return nullptr; }
    construct_at_end(std::forward<Args>(args)...);
    return &data()[size_ - 1U];
  }

  // The convenience path. Kept beside try_* so the trade-off stays visible
  // instead of being hidden behind one "obvious" choice.
  void push_back(const T& v) {
    if (full()) { throw std::length_error("FixedVector::push_back: full"); }
    construct_at_end(v);
  }
  void push_back(T&& v) {
    if (full()) { throw std::length_error("FixedVector::push_back: full"); }
    construct_at_end(std::move(v));
  }

  void pop_back() noexcept {
    assert(!empty());
    --size_;
    data()[size_].~T();
  }

  void clear() noexcept {
    // Destroy back-to-front: mirrors the order automatic storage is torn down in,
    // so a T whose destructor observes its neighbours sees the same sequence.
    while (size_ > 0U) { pop_back(); }
  }

 private:
  // Raw storage, NOT `T data_[Capacity]`. An array of T would default-construct
  // all Capacity elements up front — requiring T to be default-constructible and
  // doing work nobody asked for. Here an element exists only once it is pushed.
  alignas(T) std::byte storage_[Capacity * sizeof(T)];
  size_type size_ = 0U;

  // AUTOSAR C++14 A5-2-4 bans reinterpret_cast. Two static_casts through void*
  // are the compliant idiom, and they also keep -Wcast-align quiet.
  // std::launder (C++17) tells the compiler a real object now lives here, so it
  // must not reuse assumptions from before the placement new.
  [[nodiscard]] T* data() noexcept {
    return std::launder(static_cast<T*>(static_cast<void*>(storage_)));
  }
  [[nodiscard]] const T* data() const noexcept {
    return std::launder(static_cast<const T*>(static_cast<const void*>(storage_)));
  }

  template <typename... Args>
  void construct_at_end(Args&&... args) {
    ::new (static_cast<void*>(&storage_[size_ * sizeof(T)])) T(std::forward<Args>(args)...);
    ++size_;
  }

  // Exception safety: if T's copy constructor throws partway through, THIS object
  // is still under construction, so ~FixedVector will never run and the elements
  // already built would leak. Roll back by hand, then rethrow.
  // When T's copy is noexcept the try/catch costs nothing at runtime.
  template <typename It>
  void copy_from(It first, It last) {
    try {
      for (It it = first; it != last; ++it) { construct_at_end(*it); }
    } catch (...) {
      clear();
      throw;
    }
  }

  void move_from(FixedVector& other) {
    for (size_type i = 0U; i < other.size_; ++i) {
      construct_at_end(std::move(other.data()[i]));
    }
    other.clear();
  }
};

}  // namespace av

#endif  // AV_CORE_FIXED_VECTOR_HPP
