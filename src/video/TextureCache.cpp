#include "video/TextureCache.h"
#include "core/Logger.h"
#include "runtime/AssetManager.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

namespace noix::video {

ResourceHandle TextureCache::insertSlot(const core::NamespacedId &id,
                                         SDL_GPUTexture *texture,
                                         SDL_GPUSampler *sampler) {
    ResourceHandle handle;
    if (!_freeList.empty()) {
        uint32_t idx = _freeList.back();
        _freeList.pop_back();
        _slots[idx].texture = texture;
        _slots[idx].sampler = sampler;
        _slots[idx].generation++;
        handle = {idx, _slots[idx].generation};
    } else {
        handle = {static_cast<uint32_t>(_slots.size()), 0};
        _slots.push_back({texture, sampler, 0});
    }
    _idToHandle[id] = handle;
    return handle;
}

static SDL_GPUTexture *createBuiltinDefaultTexture(SDL_GPUDevice *device) {
    // 2x2 checkerboard (white + gray)
    // R8G8B8A8_UNORM: GPU reads byte0=R, byte1=G, byte2=B, byte3=A
    // On x86 LE, uint32_t 0xFFA0A0A0 → bytes A0,A0,A0,FF → R=160,G=160,B=160,A=255
    uint32_t pixels[4] = {
        0xFFFFFFFF, // white  (R=255, G=255, B=255, A=255)
        0xFFA0A0A0, // gray   (R=160, G=160, B=160, A=255)
        0xFFA0A0A0, // gray
        0xFFFFFFFF  // white
    };
    size_t dataSize = sizeof(pixels);

    SDL_GPUTextureCreateInfo texInfo{};
    texInfo.type = SDL_GPU_TEXTURETYPE_2D;
    texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texInfo.width = 2;
    texInfo.height = 2;
    texInfo.layer_count_or_depth = 1;
    texInfo.num_levels = 1;
    texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texInfo.props = 0;

    SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &texInfo);
    if (!texture) {
        core::Logger::instance().error(
            "TextureCache: Failed to create builtin default texture");
        return nullptr;
    }

    SDL_GPUTransferBufferCreateInfo xferInfo{};
    xferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    xferInfo.size = static_cast<Uint32>(dataSize);
    xferInfo.props = 0;
    SDL_GPUTransferBuffer *xferBuf = SDL_CreateGPUTransferBuffer(device, &xferInfo);
    if (!xferBuf) {
        core::Logger::instance().error(
            "TextureCache: Failed to create transfer buffer for builtin");
        SDL_ReleaseGPUTexture(device, texture);
        return nullptr;
    }

    void *mapped = SDL_MapGPUTransferBuffer(device, xferBuf, false);
    if (!mapped) {
        core::Logger::instance().error(
            "TextureCache: Failed to map transfer buffer for builtin");
        SDL_ReleaseGPUTransferBuffer(device, xferBuf);
        SDL_ReleaseGPUTexture(device, texture);
        return nullptr;
    }
    SDL_memcpy(mapped, pixels, dataSize);
    SDL_UnmapGPUTransferBuffer(device, xferBuf);

    SDL_GPUCommandBuffer *cmdBuf = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(cmdBuf);

    SDL_GPUTextureTransferInfo src{};
    src.offset = 0;
    src.transfer_buffer = xferBuf;

    SDL_GPUTextureRegion dst{};
    dst.texture = texture;
    dst.mip_level = 0;
    dst.layer = 0;
    dst.x = 0;
    dst.y = 0;
    dst.z = 0;
    dst.w = 2;
    dst.h = 2;
    dst.d = 1;

    SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(cmdBuf);
    SDL_ReleaseGPUTransferBuffer(device, xferBuf);

    return texture;
}

static SDL_GPUTexture *loadTextureFromFile(SDL_GPUDevice *device,
                                            const std::string &absolutePath) {
    SDL_Surface *surface = IMG_Load(absolutePath.c_str());
    if (!surface) {
        core::Logger::instance().error(
            "TextureCache: Failed to load image: {}", SDL_GetError());
        return nullptr;
    }

    // Convert to ABGR8888 which matches R8G8B8A8_UNORM on x86
    if (surface->format != SDL_PIXELFORMAT_ABGR8888) {
        SDL_Surface *converted =
            SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ABGR8888);
        SDL_DestroySurface(surface);
        if (!converted) {
            core::Logger::instance().error(
                "TextureCache: Failed to convert surface to ABGR8888");
            return nullptr;
        }
        surface = converted;
    }

    SDL_GPUTextureCreateInfo texInfo{};
    texInfo.type = SDL_GPU_TEXTURETYPE_2D;
    texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texInfo.width = static_cast<Uint32>(surface->w);
    texInfo.height = static_cast<Uint32>(surface->h);
    texInfo.layer_count_or_depth = 1;
    texInfo.num_levels = 1;
    texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texInfo.props = 0;

    SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &texInfo);
    if (!texture) {
        core::Logger::instance().error(
            "TextureCache: Failed to create texture: {}", SDL_GetError());
        SDL_DestroySurface(surface);
        return nullptr;
    }

    size_t dataSize = static_cast<size_t>(surface->pitch) * surface->h;
    SDL_GPUTransferBufferCreateInfo xferInfo{};
    xferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    xferInfo.size = static_cast<Uint32>(dataSize);
    xferInfo.props = 0;
    SDL_GPUTransferBuffer *xferBuf =
        SDL_CreateGPUTransferBuffer(device, &xferInfo);
    if (!xferBuf) {
        core::Logger::instance().error(
            "TextureCache: Failed to create transfer buffer");
        SDL_DestroySurface(surface);
        SDL_ReleaseGPUTexture(device, texture);
        return nullptr;
    }

    void *mapped = SDL_MapGPUTransferBuffer(device, xferBuf, false);
    if (!mapped) {
        core::Logger::instance().error(
            "TextureCache: Failed to map transfer buffer");
        SDL_ReleaseGPUTransferBuffer(device, xferBuf);
        SDL_DestroySurface(surface);
        SDL_ReleaseGPUTexture(device, texture);
        return nullptr;
    }
    SDL_memcpy(mapped, surface->pixels, dataSize);
    SDL_UnmapGPUTransferBuffer(device, xferBuf);

    SDL_GPUCommandBuffer *uploadCmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(uploadCmd);

    SDL_GPUTextureTransferInfo src{};
    src.offset = 0;
    src.transfer_buffer = xferBuf;

    SDL_GPUTextureRegion dst{};
    dst.texture = texture;
    dst.mip_level = 0;
    dst.layer = 0;
    dst.x = 0;
    dst.y = 0;
    dst.z = 0;
    dst.w = static_cast<Uint32>(surface->w);
    dst.h = static_cast<Uint32>(surface->h);
    dst.d = 1;

    SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(uploadCmd);
    SDL_ReleaseGPUTransferBuffer(device, xferBuf);
    SDL_DestroySurface(surface);

    return texture;
}

ResourceHandle TextureCache::create(const core::NamespacedId &id,
                                     SDL_GPUDevice *device,
                                     runtime::AssetManager &assetMgr) {
    if (auto existing = findById(id); existing.has_value()) {
        return *existing;
    }

    SDL_GPUTexture *texture = nullptr;

    // Builtin default texture: 1x1 white pixel
    if (id == core::NamespacedId("noix", "builtin-default")) {
        texture = createBuiltinDefaultTexture(device);
    } else {
        auto texPath = assetMgr.resolve(id);
        if (!texPath.has_value()) {
            core::Logger::instance().error(
                "TextureCache: Texture not found: {}", id.toString());
            return {};
        }
        texture = loadTextureFromFile(device, texPath->string());
    }

    if (!texture) return {};

    // Create sampler: nearest for builtin checkerboard, linear for others
    SDL_GPUSamplerCreateInfo samplerInfo{};
    if (id == core::NamespacedId("noix", "builtin-default")) {
        samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
        samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    } else {
        samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
        samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    }
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.props = 0;
    SDL_GPUSampler *sampler = SDL_CreateGPUSampler(device, &samplerInfo);
    if (!sampler) {
        core::Logger::instance().error(
            "TextureCache: Failed to create sampler for: {}", id.toString());
        SDL_ReleaseGPUTexture(device, texture);
        return {};
    }

    return insertSlot(id, texture, sampler);
}

void TextureCache::destroy(ResourceHandle handle, SDL_GPUDevice *device) {
    if (!handle.isValid() || handle.index >= _slots.size()) return;
    auto &slot = _slots[handle.index];
    if (slot.generation != handle.generation) return;

    if (slot.texture) SDL_ReleaseGPUTexture(device, slot.texture);
    if (slot.sampler) SDL_ReleaseGPUSampler(device, slot.sampler);
    slot.texture = nullptr;
    slot.sampler = nullptr;
    slot.generation++;
    _freeList.push_back(handle.index);

    for (auto it = _idToHandle.begin(); it != _idToHandle.end(); ++it) {
        if (it->second == handle) {
            _idToHandle.erase(it);
            break;
        }
    }
}

SDL_GPUTexture *TextureCache::get(ResourceHandle handle) const {
    if (!handle.isValid() || handle.index >= _slots.size()) return nullptr;
    const auto &slot = _slots[handle.index];
    if (slot.generation != handle.generation) return nullptr;
    return slot.texture;
}

SDL_GPUSampler *TextureCache::getSampler(ResourceHandle handle) const {
    if (!handle.isValid() || handle.index >= _slots.size()) return nullptr;
    const auto &slot = _slots[handle.index];
    if (slot.generation != handle.generation) return nullptr;
    return slot.sampler;
}

std::optional<ResourceHandle>
TextureCache::findById(const core::NamespacedId &id) const {
    auto it = _idToHandle.find(id);
    if (it == _idToHandle.end()) return std::nullopt;
    return it->second;
}

void TextureCache::addBuiltin(const core::NamespacedId &id) {
    _builtins.insert(id);
}

void TextureCache::update(const std::vector<core::NamespacedId> &targetIds,
                           SDL_GPUDevice *device,
                           runtime::AssetManager &assetMgr) {
    std::vector<core::NamespacedId> toRemove;
    for (auto &[id, handle] : _idToHandle) {
        if (_builtins.count(id)) continue;
        bool inTarget = false;
        for (auto &tid : targetIds) {
            if (tid == id) { inTarget = true; break; }
        }
        if (!inTarget) toRemove.push_back(id);
    }

    std::vector<core::NamespacedId> toAdd;
    for (auto &tid : targetIds) {
        if (_idToHandle.find(tid) == _idToHandle.end()) {
            toAdd.push_back(tid);
        }
    }

    for (auto &id : toRemove) {
        auto handle = _idToHandle[id];
        destroy(handle, device);
    }

    for (auto &id : toAdd) {
        create(id, device, assetMgr);
    }
}

} // namespace noix::video
