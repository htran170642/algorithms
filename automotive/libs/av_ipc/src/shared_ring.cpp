#include "av/ipc/shared_ring.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "av/ipc/unique_fd.hpp"
#include "av/log.hpp"

namespace av::ipc {
namespace {

namespace log = av::log;

/// POSIX requires a leading '/' and no other separator. Getting this wrong
/// gives EINVAL from shm_open with no hint as to why.
bool name_is_valid(std::string_view name) {
    return name.size() > 1U && name.front() == '/' &&
           name.find('/', 1U) == std::string_view::npos;
}

}  // namespace

std::optional<SharedMapping> SharedMapping::create(std::string_view name, std::size_t size) {
    if (!name_is_valid(name) || size == 0U) {
        log::error("ipc.shm", "invalid name or size", "name", name, "size", size);
        return std::nullopt;
    }
    const std::string owned{name};

    // O_EXCL so a second creator fails loudly instead of quietly sharing a ring
    // whose indices are already in use by somebody else.
    UniqueFd fd{::shm_open(owned.c_str(), O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR)};
    if (!fd) {
        log::error("ipc.shm", "shm_open create failed", "name", name, "errno",
                   std::strerror(errno));
        return std::nullopt;
    }

    // A fresh shm object has length 0; mapping it without this gives SIGBUS on
    // first touch, not an error return.
    if (::ftruncate(fd.get(), static_cast<off_t>(size)) != 0) {
        log::error("ipc.shm", "ftruncate failed", "name", name, "errno", std::strerror(errno));
        static_cast<void>(::shm_unlink(owned.c_str()));
        return std::nullopt;
    }

    void* address = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
    if (address == MAP_FAILED) {
        log::error("ipc.shm", "mmap failed", "name", name, "errno", std::strerror(errno));
        static_cast<void>(::shm_unlink(owned.c_str()));
        return std::nullopt;
    }

    return SharedMapping{std::move(fd), address, size, owned};
}

std::optional<SharedMapping> SharedMapping::open(std::string_view name) {
    if (!name_is_valid(name)) {
        return std::nullopt;
    }
    const std::string owned{name};

    UniqueFd fd{::shm_open(owned.c_str(), O_RDWR, 0)};
    if (!fd) {
        log::error("ipc.shm", "shm_open failed", "name", name, "errno", std::strerror(errno));
        return std::nullopt;
    }

    // The peer does not know the size, so ask the object itself rather than
    // trusting a number passed out of band.
    struct stat info{};
    if (::fstat(fd.get(), &info) != 0 || info.st_size <= 0) {
        log::error("ipc.shm", "fstat failed or empty region", "name", name);
        return std::nullopt;
    }
    const auto size = static_cast<std::size_t>(info.st_size);

    void* address = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
    if (address == MAP_FAILED) {
        log::error("ipc.shm", "mmap failed", "name", name, "errno", std::strerror(errno));
        return std::nullopt;
    }

    // Empty owned_name_: an opener must never unlink a name it did not create.
    return SharedMapping{std::move(fd), address, size, std::string{}};
}

SharedMapping::SharedMapping(SharedMapping&& other) noexcept
    : fd_(std::move(other.fd_)),
      address_(std::exchange(other.address_, nullptr)),
      size_(std::exchange(other.size_, 0U)),
      owned_name_(std::move(other.owned_name_)) {
    other.owned_name_.clear();
}

SharedMapping& SharedMapping::operator=(SharedMapping&& other) noexcept {
    if (this != &other) {
        unmap();
        fd_ = std::move(other.fd_);
        address_ = std::exchange(other.address_, nullptr);
        size_ = std::exchange(other.size_, 0U);
        owned_name_ = std::move(other.owned_name_);
        other.owned_name_.clear();
    }
    return *this;
}

SharedMapping::~SharedMapping() { unmap(); }

void SharedMapping::unmap() noexcept {
    if (address_ != nullptr) {
        static_cast<void>(::munmap(address_, size_));
        address_ = nullptr;
        size_ = 0U;
    }
    if (!owned_name_.empty()) {
        // Like unlink() on a socket file: removes the name, but the pages stay
        // alive until the last mapping goes away. A peer already attached is
        // unaffected; a new peer can no longer find it.
        static_cast<void>(::shm_unlink(owned_name_.c_str()));
        owned_name_.clear();
    }
}

}  // namespace av::ipc
