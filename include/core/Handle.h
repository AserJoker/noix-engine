#pragma once

/*
 * BaseHandle — Non-template base for type-erased storage in AssetManager.
 * Handle<T>  — Typed wrapper around SlotId, provides get() and unload().
 */

#include "core/SlotId.h"

namespace noix::core {

class BaseHandle {
public:
    virtual ~BaseHandle() = default;

    bool isValid() const { return _slotId.isValid(); }
    SlotId slotId() const { return _slotId; }
    uint32_t index() const { return _slotId.index; }
    uint8_t generation() const { return _slotId.generation; }

    /// Called by AssetManager to release the resource from its SlotMap.
    virtual void unload() = 0;

protected:
    explicit BaseHandle(SlotId id) : _slotId(id) {}
    SlotId _slotId;
};

template<typename T>
class Handle : public BaseHandle {
public:
    Handle() : BaseHandle(SlotId::invalid()) {}
    explicit Handle(SlotId id) : BaseHandle(id) {}

    /// Access the resource object via T::slotMap().
    T *get() const {
        if (!isValid()) return nullptr;
        return T::slotMap().get(_slotId);
    }

    /// Remove from SlotMap and invalidate.
    void unload() override {
        if (isValid()) {
            T::slotMap().remove(_slotId);
            _slotId = SlotId::invalid();
        }
    }

    bool operator==(const Handle &other) const {
        return _slotId.value == other._slotId.value;
    }
};

} // namespace noix::core
