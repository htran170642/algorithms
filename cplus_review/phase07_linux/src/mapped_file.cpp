#include "mapped_file.hpp"

#include <fcntl.h>        // open, O_RDONLY
#include <sys/mman.h>     // mmap, munmap, PROT_READ, MAP_PRIVATE, MAP_FAILED
#include <sys/resource.h> // getrusage, RUSAGE_SELF, struct rusage
#include <sys/stat.h>     // fstat, struct stat
#include <unistd.h>       // close

#include <cerrno>
#include <cstddef>
#include <system_error>
#include <utility>

namespace cr {

namespace {

[[noreturn]] void throw_errno(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}

}  // namespace

MappedFile MappedFile::open_readonly(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        throw_errno("open");
    }

    struct stat st{};
    if (::fstat(fd, &st) == -1) {
        ::close(fd);
        throw_errno("fstat");
    }
    const auto size = static_cast<std::size_t>(st.st_size);

    // mmap() of length 0 fails with EINVAL, so an empty file maps to nothing.
    if (size == 0) {
        ::close(fd);
        return MappedFile{nullptr, 0};
    }

    void* addr = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    // The mapping keeps its own reference to the file, so the fd can go now.
    ::close(fd);
    if (addr == MAP_FAILED) {
        throw_errno("mmap");
    }
    return MappedFile{addr, size};
}

MappedFile::~MappedFile() {
    if (addr_ != nullptr && size_ != 0) {
        ::munmap(addr_, size_);
    }
}

MappedFile::MappedFile(MappedFile&& other) noexcept
    : addr_(std::exchange(other.addr_, nullptr)),
      size_(std::exchange(other.size_, 0)) {}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        if (addr_ != nullptr && size_ != 0) {
            ::munmap(addr_, size_);
        }
        addr_ = std::exchange(other.addr_, nullptr);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

PageFaults read_page_faults() {
    struct rusage ru{};
    if (::getrusage(RUSAGE_SELF, &ru) == -1) {
        throw_errno("getrusage");
    }
    return {ru.ru_minflt, ru.ru_majflt};
}

}  // namespace cr
