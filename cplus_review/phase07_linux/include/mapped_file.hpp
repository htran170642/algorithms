#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace cr {

// RAII wrapper over a read-only memory mapping of an entire file
// (PROT_READ, MAP_PRIVATE). The mapping is demand-paged: mmap() only installs a
// VMA, so no file bytes are resident until a page is actually touched -- the
// first access to each page triggers a page fault that pulls it in (minor if the
// page is already in the page cache, major if it must be read from disk).
//
// Move-only: the object owns the mapping and munmap()s it in the destructor.
// A moved-from MappedFile is empty and safe to destroy.
class MappedFile {
public:
    // Maps `path` in full. Throws std::system_error if open()/fstat()/mmap()
    // fail. An empty file maps to an empty range (size()==0, data()==nullptr).
    static MappedFile open_readonly(const std::string& path);

    ~MappedFile();

    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;
    MappedFile(const MappedFile&)            = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return {static_cast<const std::byte*>(addr_), size_};
    }
    [[nodiscard]] const char* data() const noexcept {
        return static_cast<const char*>(addr_);
    }
    [[nodiscard]] std::size_t size()  const noexcept { return size_; }
    [[nodiscard]] bool        empty() const noexcept { return size_ == 0; }

private:
    MappedFile(void* addr, std::size_t size) noexcept : addr_(addr), size_(size) {}

    void*       addr_ = nullptr;  // nullptr for an empty (unmapped) file
    std::size_t size_ = 0;
};

// Page-fault counters for the current process, from getrusage(RUSAGE_SELF).
//   minor -- page was already in RAM (page cache / COW): remap only, no I/O.
//   major -- page had to be fetched from disk or swap: real I/O, ~1000x slower.
struct PageFaults {
    long minor = 0;  // ru_minflt
    long major = 0;  // ru_majflt
};

[[nodiscard]] PageFaults read_page_faults();

}  // namespace cr
