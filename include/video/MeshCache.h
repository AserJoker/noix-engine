#pragma once

/*
 * MeshCache — Slot-map based cache for GeometryDef (vertex/index buffers).
 * Supports O(1) access by handle, diff-based batch updates, and builtin resources.
 */

#include "core/NamespacedId.h"
#include "video/GeometryDef.h"
#include "video/ResourceHandle.h"

#include <optional>
#include <unordered_map>
#include <unordered_set>

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace noix::runtime { class AssetManager; }

namespace noix::video {

class MeshCache {
public:
    MeshCache() = default;

    /// Create a mesh from a NamespacedId. Returns invalid handle on failure.
    ResourceHandle create(const core::NamespacedId &id,
                          SDL_GPUDevice *device,
                          runtime::AssetManager &assetMgr);

    /// Destroy a mesh by handle. No-op if invalid.
    void destroy(ResourceHandle handle, SDL_GPUDevice *device);

    /// Get GeometryDef by handle. Returns nullptr if invalid.
    GeometryDef *get(ResourceHandle handle) const;

    /// Find handle by NamespacedId.
    std::optional<ResourceHandle> findById(const core::NamespacedId &id) const;

    /// Diff-based batch update.
    void update(const std::vector<core::NamespacedId> &targetIds,
                SDL_GPUDevice *device,
                runtime::AssetManager &assetMgr);

    /// Register a NamespacedId as builtin.
    void addBuiltin(const core::NamespacedId &id);

private:
    struct Slot {
        GeometryDef geometry;
        uint32_t generation = 0;
    };

    std::vector<Slot> _slots;
    std::vector<uint32_t> _freeList;
    std::unordered_map<core::NamespacedId, ResourceHandle> _idToHandle;
    std::unordered_set<core::NamespacedId> _builtins;
};

} // namespace noix::video
