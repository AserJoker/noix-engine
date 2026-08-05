#include "video/Texture.h"
#include "core/Logger.h"
#include "runtime/Application.h"
#include "video/Renderer.h"

#include <SDL3/SDL_gpu.h>

namespace noix::video {

Texture::Texture(const core::NamespacedId &id,
                 std::filesystem::path filePath,
                 core::ResourceMode mode,
                 SDL_GPUTexture *texture,
                 SDL_GPUSampler *sampler,
                 uint32_t width,
                 uint32_t height)
    : core::Resource(id, std::move(filePath), mode),
      _texture(texture), _sampler(sampler),
      _width(width), _height(height) {}

Texture::~Texture() {
    if (_texture || _sampler) {
        auto *device = runtime::Application::instance()
                           .renderer().gpuDevice();
        if (device) {
            if (_texture) SDL_ReleaseGPUTexture(device, _texture);
            if (_sampler) SDL_ReleaseGPUSampler(device, _sampler);
        }
        _texture = nullptr;
        _sampler = nullptr;
    }
}

Texture::Handle Texture::resolve(const core::NamespacedId &id,
                                  const SurfaceRef &surfaceRef,
                                  std::filesystem::path filePath,
                                  core::ResourceMode mode) {
    auto *device = runtime::Application::instance()
                       .renderer().gpuDevice();
    if (!device || !surfaceRef) return {};
    SDL_Surface *surface = surfaceRef.get();

    // Create GPU texture
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
            "Texture: Failed to create GPU texture: {}", SDL_GetError());
        return {};
    }

    // Upload pixel data via transfer buffer
    size_t dataSize = static_cast<size_t>(surface->pitch) * surface->h;
    SDL_GPUTransferBufferCreateInfo xferInfo{};
    xferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    xferInfo.size = static_cast<Uint32>(dataSize);
    xferInfo.props = 0;
    SDL_GPUTransferBuffer *xferBuf =
        SDL_CreateGPUTransferBuffer(device, &xferInfo);
    if (!xferBuf) {
        core::Logger::instance().error("Texture: Failed to create transfer buffer");
        SDL_ReleaseGPUTexture(device, texture);
        return {};
    }

    void *mapped = SDL_MapGPUTransferBuffer(device, xferBuf, false);
    if (!mapped) {
        core::Logger::instance().error("Texture: Failed to map transfer buffer");
        SDL_ReleaseGPUTransferBuffer(device, xferBuf);
        SDL_ReleaseGPUTexture(device, texture);
        return {};
    }
    SDL_memcpy(mapped, surface->pixels, dataSize);
    SDL_UnmapGPUTransferBuffer(device, xferBuf);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(cmd);

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

    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device, xferBuf);

    // Create sampler
    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.props = 0;
    SDL_GPUSampler *sampler = SDL_CreateGPUSampler(device, &samplerInfo);
    if (!sampler) {
        core::Logger::instance().error(
            "Texture: Failed to create sampler: {}", SDL_GetError());
        SDL_ReleaseGPUTexture(device, texture);
        return {};
    }

    Texture tex(id, std::move(filePath), mode,
                texture, sampler,
                texInfo.width, texInfo.height);
    return Handle(slotMap().insert(std::move(tex)));
}

} // namespace noix::video
