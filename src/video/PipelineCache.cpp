#include "video/PipelineCache.h"
#include "video/PipelineDef.h"
#include "core/Logger.h"
#include "runtime/AssetManager.h"

#include <SDL3/SDL.h>

#include <fstream>
#include <sstream>

namespace noix::video {

ResourceHandle PipelineCache::insertSlot(const core::NamespacedId &id,
                                          SDL_GPUGraphicsPipeline *pipeline) {
    ResourceHandle handle;
    if (!_freeList.empty()) {
        uint32_t idx = _freeList.back();
        _freeList.pop_back();
        _slots[idx].pipeline = pipeline;
        _slots[idx].generation++;
        handle = {idx, _slots[idx].generation};
    } else {
        handle = {static_cast<uint32_t>(_slots.size()), 0};
        _slots.push_back({pipeline, 0});
    }
    _idToHandle[id] = handle;
    return handle;
}

ResourceHandle PipelineCache::create(const core::NamespacedId &id,
                                      SDL_GPUDevice *device,
                                      runtime::AssetManager &assetMgr,
                                      SDL_GPUTextureFormat swapchainFmt) {
    if (auto existing = findById(id); existing.has_value()) {
        return *existing;
    }

    // Builtin pipeline: construct PipelineDef directly
    if (id == core::NamespacedId("noix", "builtin-textured")) {
        PipelineDef def;
        def._vertexShader = core::NamespacedId("noix", "shaders/textured/texture.vert.spv");
        def._fragmentShader = core::NamespacedId("noix", "shaders/textured/texture.frag.spv");
        def._primitiveType = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        def._vertexBuffers.push_back({0, 16, SDL_GPU_VERTEXINPUTRATE_VERTEX});
        def._vertexAttributes.push_back({0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0});
        def._vertexAttributes.push_back({1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 8});
        def._rasterizer.fillMode = SDL_GPU_FILLMODE_FILL;
        def._rasterizer.cullMode = SDL_GPU_CULLMODE_NONE;
        def._rasterizer.frontFace = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        ColorTargetDef ct;
        ct.format = "swapchain";
        ct.blend.emplace();
        ct.blend->srcColor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        ct.blend->dstColor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ct.blend->colorOp = SDL_GPU_BLENDOP_ADD;
        ct.blend->srcAlpha = SDL_GPU_BLENDFACTOR_ONE;
        ct.blend->dstAlpha = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ct.blend->alphaOp = SDL_GPU_BLENDOP_ADD;
        def._colorTargets.push_back(std::move(ct));
        def._multisample.sampleCount = SDL_GPU_SAMPLECOUNT_1;

        // Load shaders
        std::map<core::NamespacedId, SDL_GPUShader *> shaderMap;
        auto vsPath = assetMgr.resolve(def.vertexShader());
        if (!vsPath.has_value()) {
            core::Logger::instance().error(
                "PipelineCache: Builtin vertex shader not found");
            return {};
        }
        SDL_GPUShaderCreateInfo vsCreateInfo{};
        size_t vsSize = 0;
        void *vsCode = SDL_LoadFile(vsPath->string().c_str(), &vsSize);
        if (!vsCode) { return {}; }
        vsCreateInfo.code = static_cast<const Uint8 *>(vsCode);
        vsCreateInfo.code_size = vsSize;
        vsCreateInfo.entrypoint = "main";
        vsCreateInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsCreateInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsCreateInfo.num_samplers = 0;
        vsCreateInfo.num_uniform_buffers = 1;
        SDL_GPUShader *vs = SDL_CreateGPUShader(device, &vsCreateInfo);
        SDL_free(vsCode);
        if (!vs) { return {}; }
        shaderMap[def.vertexShader()] = vs;

        auto fsPath = assetMgr.resolve(*def.fragmentShader());
        if (!fsPath.has_value()) {
            SDL_ReleaseGPUShader(device, vs);
            return {};
        }
        SDL_GPUShaderCreateInfo fsCreateInfo{};
        size_t fsSize = 0;
        void *fsCode = SDL_LoadFile(fsPath->string().c_str(), &fsSize);
        if (!fsCode) {
            SDL_ReleaseGPUShader(device, vs);
            return {};
        }
        fsCreateInfo.code = static_cast<const Uint8 *>(fsCode);
        fsCreateInfo.code_size = fsSize;
        fsCreateInfo.entrypoint = "main";
        fsCreateInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsCreateInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fsCreateInfo.num_samplers = 1;
        fsCreateInfo.num_uniform_buffers = 0;
        SDL_GPUShader *fs = SDL_CreateGPUShader(device, &fsCreateInfo);
        SDL_free(fsCode);
        if (!fs) {
            SDL_ReleaseGPUShader(device, vs);
            return {};
        }
        shaderMap[*def.fragmentShader()] = fs;

        SDL_GPUGraphicsPipeline *pipeline =
            def.createPipeline(device, shaderMap, swapchainFmt);
        for (auto &[sid, shader] : shaderMap) {
            SDL_ReleaseGPUShader(device, shader);
        }
        if (!pipeline) { return {}; }
        return insertSlot(id, pipeline);
    }

    // Non-builtin: load from AssetManager
    auto pipelinePath = assetMgr.resolve(id);
    if (!pipelinePath.has_value()) {
        core::Logger::instance().error(
            "PipelineCache: Pipeline not found: {}", id.toString());
        return {};
    }

    std::ifstream file(pipelinePath->string());
    if (!file.is_open()) {
        core::Logger::instance().error(
            "PipelineCache: Cannot open pipeline file: {}",
            pipelinePath->string());
        return {};
    }
    std::stringstream buf;
    buf << file.rdbuf();
    auto pipelineVal = core::Value::parse(buf.str());
    auto pipelineDef = PipelineDef::parse(pipelineVal);
    if (!pipelineDef.has_value()) {
        core::Logger::instance().error(
            "PipelineCache: Failed to parse pipeline: {}", id.toString());
        return {};
    }

    // Load shaders
    std::map<core::NamespacedId, SDL_GPUShader *> shaderMap;
    auto vsPath = assetMgr.resolve(pipelineDef->vertexShader());
    if (!vsPath.has_value()) {
        core::Logger::instance().error(
            "PipelineCache: Vertex shader not found: {}",
            pipelineDef->vertexShader().toString());
        return {};
    }

    // Load vertex shader
    SDL_GPUShaderCreateInfo vsCreateInfo{};
    size_t vsSize = 0;
    void *vsCode = SDL_LoadFile(vsPath->string().c_str(), &vsSize);
    if (!vsCode) {
        core::Logger::instance().error(
            "PipelineCache: Failed to read vertex shader: {}",
            vsPath->string());
        return {};
    }
    vsCreateInfo.code = static_cast<const Uint8 *>(vsCode);
    vsCreateInfo.code_size = vsSize;
    vsCreateInfo.entrypoint = "main";
    vsCreateInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vsCreateInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vsCreateInfo.num_samplers = 0;
    vsCreateInfo.num_uniform_buffers = 1;
    SDL_GPUShader *vs = SDL_CreateGPUShader(device, &vsCreateInfo);
    SDL_free(vsCode);
    if (!vs) {
        core::Logger::instance().error(
            "PipelineCache: Failed to create vertex shader: {}",
            vsPath->string());
        return {};
    }
    shaderMap[pipelineDef->vertexShader()] = vs;

    // Load fragment shader (optional)
    if (pipelineDef->fragmentShader().has_value()) {
        auto fsPath = assetMgr.resolve(*pipelineDef->fragmentShader());
        if (!fsPath.has_value()) {
            core::Logger::instance().error(
                "PipelineCache: Fragment shader not found: {}",
                pipelineDef->fragmentShader()->toString());
            SDL_ReleaseGPUShader(device, vs);
            return {};
        }
        SDL_GPUShaderCreateInfo fsCreateInfo{};
        size_t fsSize = 0;
        void *fsCode = SDL_LoadFile(fsPath->string().c_str(), &fsSize);
        if (!fsCode) {
            core::Logger::instance().error(
                "PipelineCache: Failed to read fragment shader: {}",
                fsPath->string());
            SDL_ReleaseGPUShader(device, vs);
            return {};
        }
        fsCreateInfo.code = static_cast<const Uint8 *>(fsCode);
        fsCreateInfo.code_size = fsSize;
        fsCreateInfo.entrypoint = "main";
        fsCreateInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsCreateInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fsCreateInfo.num_samplers = 1;
        fsCreateInfo.num_uniform_buffers = 0;
        SDL_GPUShader *fs = SDL_CreateGPUShader(device, &fsCreateInfo);
        SDL_free(fsCode);
        if (!fs) {
            core::Logger::instance().error(
                "PipelineCache: Failed to create fragment shader: {}",
                fsPath->string());
            SDL_ReleaseGPUShader(device, vs);
            return {};
        }
        shaderMap[*pipelineDef->fragmentShader()] = fs;
    }

    SDL_GPUGraphicsPipeline *pipeline =
        pipelineDef->createPipeline(device, shaderMap, swapchainFmt);

    // Release shader objects (pipeline owns a copy internally)
    for (auto &[sid, shader] : shaderMap) {
        SDL_ReleaseGPUShader(device, shader);
    }

    if (!pipeline) {
        core::Logger::instance().error(
            "PipelineCache: Failed to create pipeline: {}", id.toString());
        return {};
    }

    return insertSlot(id, pipeline);
}

void PipelineCache::destroy(ResourceHandle handle, SDL_GPUDevice *device) {
    if (!handle.isValid() || handle.index >= _slots.size()) return;
    auto &slot = _slots[handle.index];
    if (slot.generation != handle.generation) return;

    if (slot.pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(device, slot.pipeline);
    }
    slot.pipeline = nullptr;
    slot.generation++;
    _freeList.push_back(handle.index);

    // Remove from idToHandle
    for (auto it = _idToHandle.begin(); it != _idToHandle.end(); ++it) {
        if (it->second == handle) {
            _idToHandle.erase(it);
            break;
        }
    }
}

SDL_GPUGraphicsPipeline *PipelineCache::get(ResourceHandle handle) const {
    if (!handle.isValid() || handle.index >= _slots.size()) return nullptr;
    const auto &slot = _slots[handle.index];
    if (slot.generation != handle.generation) return nullptr;
    return slot.pipeline;
}

std::optional<ResourceHandle>
PipelineCache::findById(const core::NamespacedId &id) const {
    auto it = _idToHandle.find(id);
    if (it == _idToHandle.end()) return std::nullopt;
    return it->second;
}

void PipelineCache::addBuiltin(const core::NamespacedId &id) {
    _builtins.insert(id);
}

void PipelineCache::update(const std::vector<core::NamespacedId> &targetIds,
                            SDL_GPUDevice *device,
                            runtime::AssetManager &assetMgr,
                            SDL_GPUTextureFormat swapchainFmt) {
    // Compute toRemove: current - target - builtins
    std::vector<core::NamespacedId> toRemove;
    for (auto &[id, handle] : _idToHandle) {
        if (_builtins.count(id)) continue;
        bool inTarget = false;
        for (auto &tid : targetIds) {
            if (tid == id) { inTarget = true; break; }
        }
        if (!inTarget) toRemove.push_back(id);
    }

    // Compute toAdd: target - current
    std::vector<core::NamespacedId> toAdd;
    for (auto &tid : targetIds) {
        if (_idToHandle.find(tid) == _idToHandle.end()) {
            toAdd.push_back(tid);
        }
    }

    // Remove stale (skip builtins)
    for (auto &id : toRemove) {
        auto handle = _idToHandle[id];
        destroy(handle, device);
    }

    // Create new
    for (auto &id : toAdd) {
        create(id, device, assetMgr, swapchainFmt);
    }
}

} // namespace noix::video
