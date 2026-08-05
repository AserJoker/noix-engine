#include "video/Shader.h"
#include "core/Logger.h"
#include "runtime/Application.h"
#include "runtime/AssetManager.h"
#include "video/Renderer.h"

#include <SDL3/SDL_gpu.h>

namespace noix::video {

SDL_GPUShader *Shader::loadFromAsset(runtime::AssetManager &assetMgr,
                                      const core::NamespacedId &id,
                                      SDL_GPUShaderStage stage,
                                      uint32_t numSamplers,
                                      uint32_t numUniformBuffers) {
    // Check builtin data first (embedded in binary, no disk access)
    const auto *builtinData = assetMgr.getBuiltinData(id);
    std::vector<uint8_t> data;
    if (builtinData) {
        data = *builtinData;
    } else {
        auto path = assetMgr.resolve(id);
        if (!path.has_value()) {
            core::Logger::instance().error(
                "Shader: Not found: {}", id.toString());
            return nullptr;
        }
        size_t size = 0;
        void *code = SDL_LoadFile(path->string().c_str(), &size);
        if (!code) {
            core::Logger::instance().error(
                "Shader: Failed to read: {}", path->string());
            return nullptr;
        }
        data.assign(static_cast<const uint8_t *>(code),
                    static_cast<const uint8_t *>(code) + size);
        SDL_free(code);
    }

    auto handle = resolve(id, std::move(data), "",
                           core::ResourceMode::Dynamic,
                           stage, numSamplers, numUniformBuffers);
    if (!handle.isValid()) return nullptr;

    Shader *shader = handle.get();
    return shader ? shader->gpuShader() : nullptr;
}

Shader::Shader(const core::NamespacedId &id,
               std::filesystem::path filePath,
               core::ResourceMode mode,
               SDL_GPUShader *shader,
               SDL_GPUShaderStage stage,
               uint32_t numSamplers,
               uint32_t numUniformBuffers)
    : core::Resource(id, std::move(filePath), mode),
      _shader(shader), _stage(stage),
      _numSamplers(numSamplers), _numUniformBuffers(numUniformBuffers) {}

Shader::~Shader() {
    if (_shader) {
        auto *device = runtime::Application::instance()
                           .renderer().gpuDevice();
        if (device) {
            SDL_ReleaseGPUShader(device, _shader);
        }
        _shader = nullptr;
    }
}

Shader::Shader(Shader &&other) noexcept
    : core::Resource(std::move(other)),
      _shader(other._shader),
      _stage(other._stage),
      _numSamplers(other._numSamplers),
      _numUniformBuffers(other._numUniformBuffers) {
    other._shader = nullptr;
    other._numSamplers = 0;
    other._numUniformBuffers = 0;
}

Shader &Shader::operator=(Shader &&other) noexcept {
    if (this != &other) {
        if (_shader) {
            auto *device = runtime::Application::instance()
                               .renderer().gpuDevice();
            if (device) {
                SDL_ReleaseGPUShader(device, _shader);
            }
        }
        core::Resource::operator=(std::move(other));
        _shader = other._shader;
        _stage = other._stage;
        _numSamplers = other._numSamplers;
        _numUniformBuffers = other._numUniformBuffers;
        other._shader = nullptr;
        other._numSamplers = 0;
        other._numUniformBuffers = 0;
    }
    return *this;
}

Shader::Handle Shader::resolve(const core::NamespacedId &id,
                                std::vector<uint8_t> data,
                                std::filesystem::path filePath,
                                core::ResourceMode mode,
                                SDL_GPUShaderStage stage,
                                uint32_t numSamplers,
                                uint32_t numUniformBuffers) {
    auto *device = runtime::Application::instance()
                       .renderer().gpuDevice();
    if (!device || data.empty()) return {};

    SDL_GPUShaderCreateInfo createInfo{};
    createInfo.code = data.data();
    createInfo.code_size = data.size();
    createInfo.entrypoint = "main";
    createInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    createInfo.stage = stage;
    createInfo.num_samplers = numSamplers;
    createInfo.num_uniform_buffers = numUniformBuffers;

    SDL_GPUShader *shader = SDL_CreateGPUShader(device, &createInfo);
    if (!shader) {
        core::Logger::instance().error(
            "Shader: Failed to create GPU shader: {}", SDL_GetError());
        return {};
    }

    Shader s(id, std::move(filePath), mode,
             shader, stage, numSamplers, numUniformBuffers);
    return Handle(slotMap().insert(std::move(s)));
}

} // namespace noix::video
