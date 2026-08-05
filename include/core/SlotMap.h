#pragma once

/*
 * SlotMap<T> — Generic slot-map with O(1) insert/get/remove.
 * Uses SlotId (24-bit index + 8-bit generation) for access.
 * Does NOT depend on Handle; pure data structure.
 * T must be MoveConstructible. T does NOT need to be default-constructible.
 */

#include "core/SlotId.h"

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

    /// Remove all entries. Destructors of stored objects are called.
    void clear() {
        _slots.clear();
        _freeList.clear();
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
