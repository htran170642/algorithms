#pragma once

// An open-addressing hash map with linear probing.
//
// Everything lives in ONE contiguous array — no per-element nodes, no pointer
// chasing. That is the whole reason it beats std::unordered_map, which the
// standard forces to be node-based (bucket interface + reference stability).
//
// Thrown away after this week. You use std::unordered_map (or absl) afterward.

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace cr {

template <typename Key, typename Value, typename Hash = std::hash<Key>>
class HashMap {
    enum class State : unsigned char { Empty, Occupied, Tombstone };

    struct Slot {
        Key   key{};
        Value value{};
        State state = State::Empty;
    };

public:
    explicit HashMap(std::size_t capacity = 16)
        : slots_(nextPow2(capacity)) {}

    void insert(const Key& k, const Value& v) {
        if (loadFactor() > 0.7) {
            rehash(slots_.size() * 2);
        }
        std::size_t i = indexOf(k);
        while (slots_[i].state == State::Occupied) {
            if (slots_[i].key == k) {           // key exists -> overwrite
                slots_[i].value = v;
                return;
            }
            i = (i + 1) & mask();               // linear probe: just the next slot
        }
        slots_[i] = Slot{k, v, State::Occupied};
        ++size_;
    }

    [[nodiscard]] std::optional<Value> find(const Key& k) const {
        std::size_t i = indexOf(k);
        std::size_t probes = 0;
        while (slots_[i].state != State::Empty && probes < slots_.size()) {
            if (slots_[i].state == State::Occupied && slots_[i].key == k) {
                return slots_[i].value;
            }
            i = (i + 1) & mask();
            ++probes;
        }
        return std::nullopt;
    }

    bool erase(const Key& k) {
        std::size_t i = indexOf(k);
        std::size_t probes = 0;
        while (slots_[i].state != State::Empty && probes < slots_.size()) {
            if (slots_[i].state == State::Occupied && slots_[i].key == k) {
                slots_[i].state = State::Tombstone;   // NOT Empty — would break probing
                --size_;
                return true;
            }
            i = (i + 1) & mask();
            ++probes;
        }
        return false;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size(); }
    [[nodiscard]] double loadFactor() const noexcept {
        return static_cast<double>(size_) / static_cast<double>(slots_.size());
    }

private:
    [[nodiscard]] std::size_t mask() const noexcept { return slots_.size() - 1; }

    [[nodiscard]] std::size_t indexOf(const Key& k) const {
        return Hash{}(k) & mask();          // power-of-2 size -> & instead of %
    }

    static std::size_t nextPow2(std::size_t n) {
        std::size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    void rehash(std::size_t new_cap) {
        std::vector<Slot> old = std::move(slots_);
        slots_ = std::vector<Slot>(new_cap);
        size_ = 0;
        for (const auto& s : old) {
            if (s.state == State::Occupied) {
                insert(s.key, s.value);
            }
        }
    }

    std::vector<Slot> slots_;
    std::size_t       size_ = 0;
};

}  // namespace cr
