#pragma once

/*
 * SlotId — 24-bit slot index + 8-bit generation packed into uint32_t.
 * Lightweight value type for O(1) slot-map access with use-after-free protection.
 */

#include <cstdint>

namespace noix::core {

union SlotId {
    struct {
        uint32_t index : 24;      // max 16M slots
        uint32_t generation : 8;  // 0-255 wrap, prevents use-after-free
    };
    uint32_t value = 0xFFFFFFFF;  // invalid: index=0xFFF..., gen=0xFF

    bool isValid() const { return value != 0xFFFFFFFF; }

    bool operator==(const SlotId &other) const { return value == other.value; }
    bool operator!=(const SlotId &other) const { return value != other.value; }

    static SlotId invalid() { return SlotId{}; }

    static SlotId make(uint32_t idx, uint8_t gen) {
        SlotId id;
        id.index = idx;
        id.generation = gen;
        return id;
    }
};

} // namespace noix::core
