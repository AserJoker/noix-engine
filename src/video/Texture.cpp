#include "video/Texture.h"
#include "core/Logger.h"
#include "runtime/Application.h"
#include "video/Renderer.h"

#include <SDL3/SDL_gpu.h>

namespace noix::video {

struct SurfaceDeleter {
    void operator()(SDL_Surface *s) const {
        if (s) SDL_DestroySurface(s);
    }
};

// --- Surface pixel format ↔ GPU texture format mapping ---

static SDL_GPUTextureFormat surfaceFormatToGpuFormat(SDL_PixelFormat f) {
    switch (f) {
    case SDL_PIXELFORMAT_ABGR8888:  return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    case SDL_PIXELFORMAT_ARGB8888:  return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    case SDL_PIXELFORMAT_RGBA8888:  return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    case SDL_PIXELFORMAT_BGRA8888:  return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    default:                        return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    }
}

static SDL_PixelFormat gpuFormatToSurfaceFormat(SDL_GPUTextureFormat f) {
    switch (f) {
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:  return SDL_PIXELFORMAT_ABGR8888;
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:  return SDL_PIXELFORMAT_ARGB8888;
    default:                                     return SDL_PIXELFORMAT_ABGR8888;
    }
}

// ---------------------------------------------------------------------------

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

Texture::Texture(Texture &&other) noexcept
    : core::Resource(std::move(other)),
      _texture(other._texture),
      _sampler(other._sampler),
      _width(other._width),
      _height(other._height) {
    other._texture = nullptr;
    other._sampler = nullptr;
    other._width = 0;
    other._height = 0;
}

Texture &Texture::operator=(Texture &&other) noexcept {
    if (this != &other) {
        // Release current GPU resources
        if (_texture || _sampler) {
            auto *device = runtime::Application::instance()
                               .renderer().gpuDevice();
            if (device) {
                if (_texture) SDL_ReleaseGPUTexture(device, _texture);
                if (_sampler) SDL_ReleaseGPUSampler(device, _sampler);
            }
        }
        core::Resource::operator=(std::move(other));
        _texture = other._texture;
        _sampler = other._sampler;
        _width = other._width;
        _height = other._height;
        other._texture = nullptr;
        other._sampler = nullptr;
        other._width = 0;
        other._height = 0;
    }
    return *this;
}

Texture::Handle Texture::resolve(const core::NamespacedId &id,
                                  std::filesystem::path filePath,
                                  core::ResourceMode mode,
                                  std::optional<SDL_GPUTextureFormat> targetFormat,
                                  SDL_GPUFilter minFilter,
                                  SDL_GPUFilter magFilter) {
    // Load image from file, then delegate to create
    auto imgHandle = Image::resolve(id, filePath, mode);
    if (!imgHandle.isValid()) return {};

    SurfaceRef surface = imgHandle.get()->surface();
    if (!surface) return {};

    return create(id, surface, targetFormat, minFilter, magFilter);
}

Texture::Handle Texture::create(const core::NamespacedId &id,
                                 const SurfaceRef &surfaceRef,
                                 std::optional<SDL_GPUTextureFormat> targetFormat,
                                 SDL_GPUFilter minFilter,
                                 SDL_GPUFilter magFilter) {
    auto *device = runtime::Application::instance()
                       .renderer().gpuDevice();
    if (!device || !surfaceRef) return {};
    SDL_Surface *rawSurface = surfaceRef.get();

    // Determine GPU format and convert surface if needed
    SDL_GPUTextureFormat gpuFormat;
    SDL_Surface *uploadSurface = rawSurface;
    SurfaceRef convertedRef;

    if (targetFormat.has_value()) {
        // Format specified by material: convert surface to match
        gpuFormat = *targetFormat;
        SDL_PixelFormat targetPixelFmt = gpuFormatToSurfaceFormat(gpuFormat);
        if (rawSurface->format != targetPixelFmt) {
            SDL_Surface *converted =
                SDL_ConvertSurface(rawSurface, targetPixelFmt);
            if (!converted) {
                core::Logger::instance().error(
                    "Texture: Failed to convert surface for target format");
                return {};
            }
            convertedRef = SurfaceRef(converted, SurfaceDeleter{});
            uploadSurface = converted;
        }
    } else {
        // No format specified: preserve original surface format
        gpuFormat = surfaceFormatToGpuFormat(rawSurface->format);
    }

    // Create GPU texture
    SDL_GPUTextureCreateInfo texInfo{};
    texInfo.type = SDL_GPU_TEXTURETYPE_2D;
    texInfo.format = gpuFormat;
    texInfo.width = static_cast<Uint32>(uploadSurface->w);
    texInfo.height = static_cast<Uint32>(uploadSurface->h);
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
    size_t dataSize = static_cast<size_t>(uploadSurface->pitch) * uploadSurface->h;
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
    SDL_memcpy(mapped, uploadSurface->pixels, dataSize);
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
    dst.w = static_cast<Uint32>(uploadSurface->w);
    dst.h = static_cast<Uint32>(uploadSurface->h);
    dst.d = 1;

    SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
    SDL_EndGPUCopyPass(copyPass);

    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device, xferBuf);

    // Create sampler
    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = minFilter;
    samplerInfo.mag_filter = magFilter;
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

    Texture tex(id, "", core::ResourceMode::Dynamic,
                texture, sampler,
                texInfo.width, texInfo.height);
    return Handle(slotMap().insert(std::move(tex)));
}

} // namespace noix::video
