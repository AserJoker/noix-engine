#pragma once

/*
 * SlotMap<T> — Generic slot-map with O(1) insert/get/remove.
 * Uses SlotId (24-bit index + 8-bit generation) for access.
 * Does NOT depend on Handle; pure data structure.
 * T must be MoveConstructible. T does NOT need to be default-constructible.
 *
 * compact() — GC: shrinks the vector by removing trailing free slots.
 * Only safe when no live Handle references trailing indices.
 */

#include "core/SlotId.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace noix::core {

template<typename T>
class SlotMap {
public:
    /// Insert an object, returns its SlotId.
    SlotId insert(T &&value) {
        if (!_freeList.empty()) {
            uint32_t idx = _freeList.back();
            _freeList.pop_back();
            _slots[idx].value.emplace(std::move(value));
            _slots[idx].occupied = true;
            return SlotId::make(idx, _slots[idx].generation);
        }
        uint32_t idx = static_cast<uint32_t>(_slots.size());
        Slot s;
        s.value.emplace(std::move(value));
        s.generation = 0;
        s.occupied = true;
        _slots.push_back(std::move(s));
        return SlotId::make(idx, 0);
    }

    /// Remove an object by SlotId. Generation increments on removal.
    bool remove(SlotId id) {
        if (!id.isValid() || id.index >= _slots.size()) return false;
        auto &slot = _slots[id.index];
        if (!slot.occupied || slot.generation != id.generation) return false;
        slot.value.reset();
        slot.occupied = false;
        slot.generation = (slot.generation + 1) & 0xFF;
        _freeList.push_back(id.index);
        return true;
    }

    /// Look up by SlotId. Returns nullptr if generation mismatch or not occupied.
    T *get(SlotId id) {
        if (!id.isValid() || id.index >= _slots.size()) return nullptr;
        auto &slot = _slots[id.index];
        if (!slot.occupied || slot.generation != id.generation) return nullptr;
        return &*slot.value;
    }

    const T *get(SlotId id) const {
        if (!id.isValid() || id.index >= _slots.size()) return nullptr;
        auto &slot = _slots[id.index];
        if (!slot.occupied || slot.generation != id.generation) return nullptr;
        return &*slot.value;
    }

    /// Number of occupied slots.
    size_t size() const {
        return _slots.size() - _freeList.size();
    }

    /// Total capacity (occupied + free).
    size_t capacity() const {
        return _slots.size();
    }

    /// Remove all entries. Destructors of stored objects are called.
    void clear() {
        _slots.clear();
        _freeList.clear();
    }

    /// Shrink the vector by removing trailing free slots.
    /// Scans from the end, pops consecutive unoccupied slots,
    /// removes their indices from the free list, then shrinks the vector.
    /// Returns the number of slots removed.
    size_t compact() {
        if (_slots.empty()) return 0;

        size_t removed = 0;
        while (!_slots.empty()) {
            auto &slot = _slots.back();
            if (slot.occupied) break;
            uint32_t idx = static_cast<uint32_t>(_slots.size() - 1);

            // Remove this index from the free list
            auto it = std::find(_freeList.begin(), _freeList.end(), idx);
            if (it != _freeList.end()) {
                _freeList.erase(it);
            }

            _slots.pop_back();
            ++removed;
        }

        // Shrink vectors to fit
        _slots.shrink_to_fit();
        _freeList.shrink_to_fit();
        return removed;
    }

private:
    struct Slot {
        std::optional<T> value;
        uint8_t generation = 0;
        bool occupied = false;
    };
    std::vector<Slot> _slots;
    std::vector<uint32_t> _freeList;
};

} // namespace noix::core
