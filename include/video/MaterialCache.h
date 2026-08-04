#pragma once

/*
 * MaterialCache — Slot-map based cache for MaterialDef.
 * Supports O(1) access by handle, diff-based batch updates.
 */

#include "core/NamespacedId.h"
#include "video/MaterialDef.h"
#include "video/ResourceHandle.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace noix::runtime { class AssetManager; }

namespace noix::video {

class MaterialCache {
public:
    MaterialCache() = default;

    /// Create a material from a NamespacedId. Returns invalid handle on failure.
    ResourceHandle create(const core::NamespacedId &id,
                          runtime::AssetManager &assetMgr);

    /// Destroy a material by handle. No-op if invalid.
    void destroy(ResourceHandle handle);

    /// Get MaterialDef by handle. Returns nullptr if invalid.
    MaterialDef *get(ResourceHandle handle) const;

    /// Find handle by NamespacedId.
    std::optional<ResourceHandle> findById(const core::NamespacedId &id) const;

    /// Diff-based batch update.
    void update(const std::vector<core::NamespacedId> &targetIds,
                runtime::AssetManager &assetMgr);

    /// Register a NamespacedId as builtin.
    void addBuiltin(const core::NamespacedId &id);

private:
    struct Slot {
        MaterialDef material;
        uint32_t generation = 0;
    };

    std::vector<Slot> _slots;
    std::vector<uint32_t> _freeList;
    std::unordered_map<core::NamespacedId, ResourceHandle> _idToHandle;
    std::unordered_set<core::NamespacedId> _builtins;
};

} // namespace noix::video
