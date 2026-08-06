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

// --- String ↔ SDL enum conversion ---

static SDL_GPUFilter toSDLFilter(const std::string &s, SDL_GPUFilter fallback) {
    if (s == "nearest")      return SDL_GPU_FILTER_NEAREST;
    if (s == "linear")       return SDL_GPU_FILTER_LINEAR;
    return fallback;
}

static SDL_GPUSamplerAddressMode toSDLSamplerAddressMode(const std::string &s,
                                                          SDL_GPUSamplerAddressMode fallback) {
    if (s == "repeat")           return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    if (s == "mirror_repeat")    return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    if (s == "clamp_to_edge")    return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    return fallback;
}

static std::optional<SDL_GPUTextureFormat> parseTextureFormat(const std::string &s) {
    if (s.empty()) return std::nullopt;
    if (s == "r8g8b8a8_unorm")    return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    if (s == "b8g8r8a8_unorm")    return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    if (s == "r8_unorm")          return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    if (s == "r8g8_unorm")        return SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
    if (s == "r8g8b8a8_snorm")    return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM;
    if (s == "r16_float")         return SDL_GPU_TEXTUREFORMAT_R16_FLOAT;
    if (s == "r16g16_float")      return SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
    if (s == "r16g16b16a16_float") return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    if (s == "r32_float")         return SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    if (s == "r32g32_float")      return SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
    if (s == "r32g32b32a32_float") return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    core::Logger::instance().warn("Texture: Unknown format '{}', ignoring", s);
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Internal: upload surface data to GPU texture + create sampler
// ---------------------------------------------------------------------------

struct GpuTextureResult {
    SDL_GPUTexture *texture = nullptr;
    SDL_GPUSampler *sampler = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
};

static std::optional<GpuTextureResult> uploadToGpu(
                       SDL_Surface *uploadSurface,
                       SDL_GPUTextureFormat gpuFormat,
                       SDL_GPUFilter minFilter,
                       SDL_GPUFilter magFilter,
                       SDL_GPUSamplerAddressMode addressModeU,
                       SDL_GPUSamplerAddressMode addressModeV) {
    auto *device = runtime::Application::instance()
                       .renderer().gpuDevice();
    if (!device) return std::nullopt;

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
        return std::nullopt;
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
        return std::nullopt;
    }

    void *mapped = SDL_MapGPUTransferBuffer(device, xferBuf, false);
    if (!mapped) {
        core::Logger::instance().error("Texture: Failed to map transfer buffer");
        SDL_ReleaseGPUTransferBuffer(device, xferBuf);
        SDL_ReleaseGPUTexture(device, texture);
        return std::nullopt;
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
    samplerInfo.address_mode_u = addressModeU;
    samplerInfo.address_mode_v = addressModeV;
    samplerInfo.props = 0;
    SDL_GPUSampler *sampler = SDL_CreateGPUSampler(device, &samplerInfo);
    if (!sampler) {
        core::Logger::instance().error(
            "Texture: Failed to create sampler: {}", SDL_GetError());
        SDL_ReleaseGPUTexture(device, texture);
        return std::nullopt;
    }

    return GpuTextureResult{texture, sampler, texInfo.width, texInfo.height};
}

// ---------------------------------------------------------------------------
// Resource protocol
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
                                  SDL_GPUFilter magFilter,
                                  SDL_GPUSamplerAddressMode addressModeU,
                                  SDL_GPUSamplerAddressMode addressModeV) {
    // Load image from file, then delegate to create
    auto imgHandle = Image::resolve(id, filePath, mode);
    if (!imgHandle.isValid()) return {};

    SurfaceRef surface = imgHandle.get()->surface();
    if (!surface) return {};

    return create(id, surface, targetFormat, minFilter, magFilter,
                  addressModeU, addressModeV);
}

Texture::Handle Texture::create(const core::NamespacedId &id,
                                 const SurfaceRef &surfaceRef,
                                 std::optional<SDL_GPUTextureFormat> targetFormat,
                                 SDL_GPUFilter minFilter,
                                 SDL_GPUFilter magFilter,
                                 SDL_GPUSamplerAddressMode addressModeU,
                                 SDL_GPUSamplerAddressMode addressModeV) {
    if (!surfaceRef) return {};
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

    auto result = uploadToGpu(uploadSurface, gpuFormat,
                              minFilter, magFilter, addressModeU, addressModeV);
    if (!result) return {};
    Texture tex(id, "", core::ResourceMode::Dynamic,
                result->texture, result->sampler,
                result->width, result->height);
    return Handle(slotMap().insert(std::move(tex)));
}

Texture::Handle Texture::create(const core::NamespacedId &id,
                                 const SurfaceRef &surface,
                                 const TextureBinding &binding) {
    auto targetFormat = parseTextureFormat(binding.format);
    auto minFilter = toSDLFilter(binding.minFilter, SDL_GPU_FILTER_LINEAR);
    auto magFilter = toSDLFilter(binding.magFilter, SDL_GPU_FILTER_LINEAR);
    auto addrU = toSDLSamplerAddressMode(binding.addressModeU,
                                          SDL_GPU_SAMPLERADDRESSMODE_REPEAT);
    auto addrV = toSDLSamplerAddressMode(binding.addressModeV,
                                          SDL_GPU_SAMPLERADDRESSMODE_REPEAT);
    return create(id, surface, targetFormat, minFilter, magFilter, addrU, addrV);
}

} // namespace noix::video
