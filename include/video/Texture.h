#pragma once

/*
 * Texture — GPU texture resource (SDL_GPUTexture + SDL_GPUSampler).
 * Destructor releases GPU objects via Application singleton.
 * Supports SlotMap protocol for unified Handle-based access.
 */

#include "core/Handle.h"
#include "core/NamespacedId.h"
#include "core/Resource.h"
#include "core/SlotMap.h"
#include "video/Image.h"  // SurfaceRef
#include "video/Material.h"  // TextureBinding

#include <SDL3/SDL_gpu.h>

#include <filesystem>
#include <vector>

namespace noix::video {

class Texture : public core::Resource {
public:
    using Handle = core::Handle<Texture>;

    // --- SlotMap protocol ---

    static core::SlotMap<Texture> &slotMap() {
        static core::SlotMap<Texture> _cache;
        return _cache;
    }

    /// Create a Texture from a filePath (loads Image internally) and insert into SlotMap.
    /// Dynamic: loads and uploads immediately.
    /// Static: stores filePath only, loads on demand.
    static Handle resolve(const core::NamespacedId &id,
                          std::filesystem::path filePath,
                          core::ResourceMode mode = core::ResourceMode::Dynamic,
                          std::optional<SDL_GPUTextureFormat> targetFormat = std::nullopt,
                          SDL_GPUFilter minFilter = SDL_GPU_FILTER_LINEAR,
                          SDL_GPUFilter magFilter = SDL_GPU_FILTER_LINEAR,
                          SDL_GPUSamplerAddressMode addressModeU = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                          SDL_GPUSamplerAddressMode addressModeV = SDL_GPU_SAMPLERADDRESSMODE_REPEAT);

    /// Create a builtin Texture from a SurfaceRef (always Dynamic).
    static Handle create(const core::NamespacedId &id,
                         const SurfaceRef &surface,
                         std::optional<SDL_GPUTextureFormat> targetFormat = std::nullopt,
                         SDL_GPUFilter minFilter = SDL_GPU_FILTER_LINEAR,
                         SDL_GPUFilter magFilter = SDL_GPU_FILTER_LINEAR,
                         SDL_GPUSamplerAddressMode addressModeU = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                         SDL_GPUSamplerAddressMode addressModeV = SDL_GPU_SAMPLERADDRESSMODE_REPEAT);

    /// Create a Texture from a SurfaceRef with sampler settings from TextureBinding.
    /// Converts string fields in TextureBinding to SDL enum values.
    static Handle create(const core::NamespacedId &id,
                         const SurfaceRef &surface,
                         const TextureBinding &binding);

    // --- Accessors ---

    SDL_GPUTexture *gpuTexture() const { return _texture; }
    SDL_GPUSampler *gpuSampler() const { return _sampler; }
    uint32_t width() const { return _width; }
    uint32_t height() const { return _height; }

    ~Texture() override;

    Texture(Texture &&other) noexcept;
    Texture &operator=(Texture &&other) noexcept;

    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;

private:
    Texture(const core::NamespacedId &id,
            std::filesystem::path filePath,
            core::ResourceMode mode,
            SDL_GPUTexture *texture,
            SDL_GPUSampler *sampler,
            uint32_t width,
            uint32_t height);

    SDL_GPUTexture *_texture = nullptr;
    SDL_GPUSampler *_sampler = nullptr;
    uint32_t _width = 0;
    uint32_t _height = 0;
};

} // namespace noix::video
