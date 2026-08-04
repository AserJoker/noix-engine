#pragma once

/*
 * TextureCache — Slot-map based cache for SDL_GPUTexture + SDL_GPUSampler.
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

class TextureCache {
public:
    TextureCache() = default;

    /// Create a texture from a NamespacedId. Returns invalid handle on failure.
    ResourceHandle create(const core::NamespacedId &id,
                          SDL_GPUDevice *device,
                          runtime::AssetManager &assetMgr);

    /// Destroy a texture by handle. No-op if invalid.
    void destroy(ResourceHandle handle, SDL_GPUDevice *device);

    /// Get texture by handle. Returns nullptr if invalid.
    SDL_GPUTexture *get(ResourceHandle handle) const;

    /// Get sampler by handle. Returns nullptr if invalid.
    SDL_GPUSampler *getSampler(ResourceHandle handle) const;

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
        SDL_GPUTexture *texture = nullptr;
        SDL_GPUSampler *sampler = nullptr;
        uint32_t generation = 0;
    };

    std::vector<Slot> _slots;
    std::vector<uint32_t> _freeList;
    std::unordered_map<core::NamespacedId, ResourceHandle> _idToHandle;
    std::unordered_set<core::NamespacedId> _builtins;

    ResourceHandle insertSlot(const core::NamespacedId &id,
                              SDL_GPUTexture *texture,
                              SDL_GPUSampler *sampler);
};

} // namespace noix::video
