#pragma once

#include <cstdint>

namespace noix::video {

/// Opaque handle for GPU resources. Provides O(1) access via slot-map index
/// with a generation counter for safe access.
struct ResourceHandle {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;

    bool isValid() const { return index != UINT32_MAX; }
    bool operator==(const ResourceHandle &) const = default;
};

} // namespace noix::video
