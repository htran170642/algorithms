#pragma once

#include "common/config.hpp"
#include "common/types.hpp"
#include <array>
#include <cstddef>

namespace bptree{

class Page {
public:
    Page() = default;
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;
    Page(Page&&) = delete;
    Page& operator=(Page&&) = delete;

    void Reset() noexcept {
        data_.fill(std::byte{0});
        page_id_ = INVALID_PAGE_ID;
        pin_count_ = 0;
        is_dirty_ = false;
    }

    std::byte* GetData() noexcept {
        return data_.data();
    }
    const std::byte* GetData() const noexcept {
        return data_.data();
    }

    template <typename T>
    T* As() noexcept {
        return reinterpret_cast<T*>(data_.data());
    }
    template <typename T>
    const T* As() const noexcept {
        return reinterpret_cast<const T*>(data_.data());
    }

    page_id_t GetPageId() const noexcept {
        return page_id_;
    }
    void SetPageId(page_id_t page_id) noexcept {
        page_id_ = page_id;
    }

    uint32_t GetPinCount() const noexcept {
        return pin_count_;
    }
    void IncrPinCount() noexcept {
        ++pin_count_;
    }
    void DecrPinCount() noexcept {
        if (pin_count_ > 0) {
            --pin_count_;
        }
    }

    bool IsDirty() const noexcept {
        return is_dirty_;
    }
    void SetDirty(bool is_dirty) noexcept {
        is_dirty_ = is_dirty;
    }


private:
    alignas(8) std::array<std::byte, PAGE_SIZE> data_{};
    page_id_t page_id_ = INVALID_PAGE_ID;
    uint32_t pin_count_ = 0;
    bool is_dirty_ = false;
};
} // namespace bptree