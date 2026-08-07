#pragma once

/*
 * Handle<T> — Typed wrapper around SlotId, provides get() and isValid().
 * Validity is determined by querying T::slotMap(), not by sentinel value.
 */

#include "core/SlotId.h"

namespace noix::core {

template<typename T>
class Handle {
public:
    Handle() = default;
    explicit Handle(SlotId id) : _slotId(id) {}

    /// Check validity by querying SlotMap (generation mismatch → invalid).
    bool isValid() const {
        return T::slotMap().get(_slotId) != nullptr;
    }

    /// Access the resource object via T::slotMap().
    T *get() const {
        return T::slotMap().get(_slotId);
    }

    SlotId slotId() const { return _slotId; }
    uint32_t index() const { return _slotId.index; }
    uint8_t generation() const { return _slotId.generation; }

    bool operator==(const Handle &other) const {
        return _slotId.value == other._slotId.value;
    }
    bool operator!=(const Handle &other) const {
        return _slotId.value != other._slotId.value;
    }

private:
    SlotId _slotId{};
};

} // namespace noix::core
