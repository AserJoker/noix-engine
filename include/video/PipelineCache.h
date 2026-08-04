#pragma once

/*
 * PipelineCache — Slot-map based cache for SDL_GPUGraphicsPipeline.
 * Supports O(1) access by handle, diff-based batch updates, and builtin resources.
 */

#include "core/NamespacedId.h"
#include "video/ResourceHandle.h"

#include <SDL3/SDL_gpu.h>

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace noix::runtime { class AssetManager; }

namespace noix::video {

class PipelineCache {
public:
    PipelineCache() = default;

    /// Create a pipeline from a NamespacedId. Returns invalid handle on failure.
    ResourceHandle create(const core::NamespacedId &id,
                          SDL_GPUDevice *device,
                          runtime::AssetManager &assetMgr,
                          SDL_GPUTextureFormat swapchainFmt);

    /// Destroy a pipeline by handle. No-op if invalid.
    void destroy(ResourceHandle handle, SDL_GPUDevice *device);

    /// Get pipeline by handle. Returns nullptr if invalid.
    SDL_GPUGraphicsPipeline *get(ResourceHandle handle) const;

    /// Find handle by NamespacedId. Returns invalid handle if not found.
    std::optional<ResourceHandle> findById(const core::NamespacedId &id) const;

    /// Diff-based batch update. Destroys removed (non-builtin), creates added.
    void update(const std::vector<core::NamespacedId> &targetIds,
                SDL_GPUDevice *device,
                runtime::AssetManager &assetMgr,
                SDL_GPUTextureFormat swapchainFmt);

    /// Register a NamespacedId as builtin (cannot be removed by diff).
    void addBuiltin(const core::NamespacedId &id);

private:
    struct Slot {
        SDL_GPUGraphicsPipeline *pipeline = nullptr;
        uint32_t generation = 0;
    };

    std::vector<Slot> _slots;
    std::vector<uint32_t> _freeList;
    std::unordered_map<core::NamespacedId, ResourceHandle> _idToHandle;
    std::unordered_set<core::NamespacedId> _builtins;

    ResourceHandle insertSlot(const core::NamespacedId &id,
                              SDL_GPUGraphicsPipeline *pipeline);
};

} // namespace noix::video
