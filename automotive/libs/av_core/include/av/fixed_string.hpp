#ifndef AV_CORE_FIXED_STRING_HPP
#define AV_CORE_FIXED_STRING_HPP

#include <cassert>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace av {

/// A string with a compile-time capacity that never touches the heap, and that is
/// ALWAYS null-terminated.
///
/// WHY NOT JUST FixedVector<char, N>?
///   Because C decided a string ends at a '\0' byte. printf, strlen, open, syslog
///   — every C API reads forward until it finds one. A buffer without that byte
///   makes them run off the end into whatever memory happens to follow: garbage
///   output, a crash, or a leak of unrelated data.
///   FixedVector<char,N> makes no such guarantee. This does.
///
/// Hence the storage is Capacity + 1 bytes: Capacity characters plus the
/// terminator. FixedString<8> holds 8 characters and occupies 9 bytes.
///
/// WHEN NOT TO USE IT:
///   - Text whose length is genuinely unbounded (user input, file contents).
///     A fixed capacity there just moves the failure somewhere quieter.
///   - When you need the full std::string API (find, replace, substr...).
///   - Capacity is part of the type, so FixedString<16> and FixedString<32> are
///     different types. Pass std::string_view across interfaces instead.
template <std::size_t Capacity>
class FixedString {
  static_assert(Capacity > 0U, "FixedString<0> cannot hold anything");

 public:
  using size_type = std::size_t;

  FixedString() noexcept { buf_[0] = '\0'; }

  /// Truncates if the input does not fit. Chosen deliberately over throwing: a
  /// constructor is the one place where a caller has no good way to react, and a
  /// label being shortened is rarely worth aborting for.
  /// Use try_append() when truncation is NOT acceptable.
  explicit FixedString(std::string_view sv) noexcept {
    buf_[0] = '\0';
    static_cast<void>(append_truncating(sv));
  }

  // --- capacity ---------------------------------------------------------------
  [[nodiscard]] size_type size() const noexcept { return size_; }
  [[nodiscard]] static constexpr size_type capacity() noexcept { return Capacity; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0U; }
  [[nodiscard]] bool full() const noexcept { return size_ == Capacity; }

  // --- access -----------------------------------------------------------------
  /// Always null-terminated. Safe to hand to any C API. This is the whole point.
  [[nodiscard]] const char* c_str() const noexcept { return buf_; }

  /// The C++ way across interfaces: one type regardless of Capacity.
  [[nodiscard]] std::string_view view() const noexcept {
    return std::string_view(buf_, size_);
  }

  char operator[](size_type i) const noexcept {
    assert(i < size_);
    return buf_[i];
  }

  // --- modifiers --------------------------------------------------------------
  void clear() noexcept {
    size_ = 0U;
    buf_[0] = '\0';
  }

  /// ALL-OR-NOTHING. Returns false and changes nothing if the text does not fit.
  /// Use this where a shortened value would be WRONG rather than merely ugly:
  /// a file path, a DTC code, a device identifier.
  [[nodiscard]] bool try_append(std::string_view sv) noexcept {
    if (sv.size() > Capacity - size_) { return false; }
    std::memcpy(buf_ + size_, sv.data(), sv.size());
    size_ += sv.size();
    buf_[size_] = '\0';
    return true;
  }

  /// Appends whatever fits and REPORTS how many characters were dropped.
  /// Use this where shortening is acceptable: a log line, a display label.
  /// The return value exists so truncation is never silent.
  size_type append_truncating(std::string_view sv) noexcept {
    const size_type room = Capacity - size_;
    const size_type taken = (sv.size() < room) ? sv.size() : room;
    std::memcpy(buf_ + size_, sv.data(), taken);
    size_ += taken;
    buf_[size_] = '\0';
    return sv.size() - taken;
  }

 private:
  // +1 for the terminator. THE detail of this whole class.
  char buf_[Capacity + 1U] = {};
  size_type size_ = 0U;
};

template <std::size_t A, std::size_t B>
bool operator==(const FixedString<A>& a, const FixedString<B>& b) noexcept {
  return a.view() == b.view();
}
template <std::size_t A, std::size_t B>
bool operator!=(const FixedString<A>& a, const FixedString<B>& b) noexcept {
  return !(a == b);
}

}  // namespace av

#endif  // AV_CORE_FIXED_STRING_HPP
