#pragma once

/*
 * Shader — GPU shader resource (SDL_GPUShader).
 * Loads SPIR-V from disk, creates SDL_GPUShader.
 * Destructor releases GPU object via Application singleton.
 * Supports SlotMap protocol for unified Handle-based access.
 */

#include "core/Handle.h"
#include "core/NamespacedId.h"
#include "core/Resource.h"
#include "core/SlotMap.h"

#include <SDL3/SDL_gpu.h>

#include <filesystem>
#include <vector>

namespace noix::runtime { class AssetManager; }

namespace noix::video {

class Shader : public core::Resource {
public:
    using Handle = core::Handle<Shader>;

    // --- SlotMap protocol ---

    static core::SlotMap<Shader> &slotMap() {
        static core::SlotMap<Shader> _cache;
        return _cache;
    }

    /// Load a shader from AssetManager by NamespacedId.
    /// Convenience wrapper: resolves path, reads file, calls resolve().
    static SDL_GPUShader *loadFromAsset(runtime::AssetManager &assetMgr,
                                         const core::NamespacedId &id,
                                         SDL_GPUShaderStage stage,
                                         uint32_t numSamplers,
                                         uint32_t numUniformBuffers);

    /// Load a SPIR-V shader file and insert into SlotMap.
    static Handle resolve(const core::NamespacedId &id,
                          std::vector<uint8_t> data,
                          std::filesystem::path filePath,
                          core::ResourceMode mode = core::ResourceMode::Dynamic,
                          SDL_GPUShaderStage stage = SDL_GPU_SHADERSTAGE_VERTEX,
                          uint32_t numSamplers = 0,
                          uint32_t numUniformBuffers = 0);

    // --- Accessors ---

    SDL_GPUShader *gpuShader() const { return _shader; }
    SDL_GPUShaderStage stage() const { return _stage; }
    uint32_t numSamplers() const { return _numSamplers; }
    uint32_t numUniformBuffers() const { return _numUniformBuffers; }

    ~Shader() override;

    Shader(Shader &&other) noexcept;
    Shader &operator=(Shader &&other) noexcept;

    Shader(const Shader &) = delete;
    Shader &operator=(const Shader &) = delete;

private:
    Shader(const core::NamespacedId &id,
           std::filesystem::path filePath,
           core::ResourceMode mode,
           SDL_GPUShader *shader,
           SDL_GPUShaderStage stage,
           uint32_t numSamplers,
           uint32_t numUniformBuffers);

    SDL_GPUShader *_shader = nullptr;
    SDL_GPUShaderStage _stage = SDL_GPU_SHADERSTAGE_VERTEX;
    uint32_t _numSamplers = 0;
    uint32_t _numUniformBuffers = 0;
};

} // namespace noix::video
