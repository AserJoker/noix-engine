#include "video/Renderer.h"
#include "video/PipelineDef.h"
#include "core/Logger.h"
#include "runtime/AssetManager.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <fstream>
#include <sstream>

namespace noix::video {

// --- Vertex layout: vec2 position + vec4 color ---
struct Vertex {
    float x, y;
    float r, g, b, a;
};

static const Vertex TRIANGLE_VERTICES[] = {
    {-0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f}, // Red (bottom-left)
    { 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f}, // Green (bottom-right)
    { 0.0f,  0.5f, 0.0f, 0.0f, 1.0f, 1.0f}, // Blue (top)
};

// ---------------------------------------------------------------------------

SDL_GPUShader *Renderer::loadShader(const std::string &absolutePath,
                                     SDL_GPUShaderStage stage) {
    size_t codeSize = 0;
    void *code = SDL_LoadFile(absolutePath.c_str(), &codeSize);
    if (!code) {
        core::Logger::instance().error("Renderer: Failed to read shader: {}",
                                       absolutePath);
        return nullptr;
    }

    SDL_GPUShaderCreateInfo createInfo{};
    createInfo.code = static_cast<const Uint8 *>(code);
    createInfo.code_size = codeSize;
    createInfo.entrypoint = "main";
    createInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    createInfo.stage = stage;

    SDL_GPUShader *shader = SDL_CreateGPUShader(_device, &createInfo);
    SDL_free(code);

    if (!shader) {
        core::Logger::instance().error(
            "Renderer: Failed to create shader from: {}", absolutePath);
    }
    return shader;
}

// ---------------------------------------------------------------------------

bool Renderer::init(SDL_Window *window, runtime::AssetManager &assetMgr) {
    _window = window;

    _device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
    if (!_device) {
        core::Logger::instance().error(
            "Renderer: SDL_CreateGPUDevice failed: {}", SDL_GetError());
        return false;
    }

    if (!SDL_ClaimWindowForGPUDevice(_device, _window)) {
        core::Logger::instance().error(
            "Renderer: SDL_ClaimWindowForGPUDevice failed: {}", SDL_GetError());
        SDL_DestroyGPUDevice(_device);
        _device = nullptr;
        return false;
    }

    // --- Load pipeline definition from JSON ---
    auto pipelinePath = assetMgr.resolve(
        core::NamespacedId("noix", "pipelines/solid.json"));
    if (!pipelinePath.has_value()) {
        core::Logger::instance().error(
            "Renderer: Pipeline definition not found: noix:pipelines/solid.json");
        shutdown();
        return false;
    }

    // Read and parse the JSON file
    std::ifstream file(*pipelinePath);
    if (!file.is_open()) {
        core::Logger::instance().error(
            "Renderer: Cannot open pipeline file: {}", pipelinePath->string());
        shutdown();
        return false;
    }
    std::stringstream buf;
    buf << file.rdbuf();
    auto pipelineVal = core::Value::parse(buf.str());
    auto pipelineDef = PipelineDef::parse(pipelineVal);
    if (!pipelineDef.has_value()) {
        core::Logger::instance().error(
            "Renderer: Failed to parse pipeline definition");
        shutdown();
        return false;
    }

    // --- Load shaders referenced by the pipeline definition ---
    auto vsPath = assetMgr.resolve(pipelineDef->vertexShader);
    if (!vsPath.has_value()) {
        core::Logger::instance().error(
            "Renderer: Vertex shader not found: {}", pipelineDef->vertexShader.toString());
        shutdown();
        return false;
    }

    std::map<core::NamespacedId, SDL_GPUShader *> shaderMap;
    SDL_GPUShader *vs = loadShader(vsPath->string(), SDL_GPU_SHADERSTAGE_VERTEX);
    if (!vs) {
        shutdown();
        return false;
    }
    shaderMap[pipelineDef->vertexShader] = vs;

    if (pipelineDef->fragmentShader.has_value()) {
        const core::NamespacedId &fsId = pipelineDef->fragmentShader.value();
        auto fsPath = assetMgr.resolve(fsId);
        if (!fsPath.has_value()) {
            core::Logger::instance().error(
                "Renderer: Fragment shader not found: {}", fsId.toString());
            shutdown();
            return false;
        }
        SDL_GPUShader *fs = loadShader(fsPath->string(), SDL_GPU_SHADERSTAGE_FRAGMENT);
        if (!fs) {
            shutdown();
            return false;
        }
        shaderMap[fsId] = fs;
    }

    // --- Create vertex buffer ---
    const Uint32 vertexDataSize = sizeof(TRIANGLE_VERTICES);

    SDL_GPUBufferCreateInfo bufInfo{};
    bufInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bufInfo.size = vertexDataSize;
    bufInfo.props = 0;
    _vertexBuffer = SDL_CreateGPUBuffer(_device, &bufInfo);
    if (!_vertexBuffer) {
        core::Logger::instance().error(
            "Renderer: Failed to create vertex buffer: {}", SDL_GetError());
        shutdown();
        return false;
    }

    // --- Upload vertex data via transfer buffer ---
    SDL_GPUTransferBufferCreateInfo xferInfo{};
    xferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    xferInfo.size = vertexDataSize;
    xferInfo.props = 0;
    SDL_GPUTransferBuffer *xferBuf =
        SDL_CreateGPUTransferBuffer(_device, &xferInfo);
    if (!xferBuf) {
        core::Logger::instance().error(
            "Renderer: Failed to create transfer buffer: {}", SDL_GetError());
        shutdown();
        return false;
    }

    void *mapped = SDL_MapGPUTransferBuffer(_device, xferBuf, false);
    SDL_memcpy(mapped, TRIANGLE_VERTICES, vertexDataSize);
    SDL_UnmapGPUTransferBuffer(_device, xferBuf);

    SDL_GPUCommandBuffer *uploadCmd = SDL_AcquireGPUCommandBuffer(_device);
    SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(uploadCmd);

    SDL_GPUTransferBufferLocation src{};
    src.offset = 0;
    src.transfer_buffer = xferBuf;

    SDL_GPUBufferRegion dst{};
    dst.buffer = _vertexBuffer;
    dst.offset = 0;
    dst.size = vertexDataSize;

    SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);
    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(uploadCmd);
    SDL_ReleaseGPUTransferBuffer(_device, xferBuf);

    // --- Create graphics pipeline from PipelineDef ---
    SDL_GPUTextureFormat swapchainFmt =
        SDL_GetGPUSwapchainTextureFormat(_device, _window);

    _pipeline = pipelineDef->createPipeline(_device, shaderMap, swapchainFmt);
    if (!_pipeline) {
        core::Logger::instance().error(
            "Renderer: Failed to create graphics pipeline: {}", SDL_GetError());
        shutdown();
        return false;
    }

    // Release shader objects (pipeline now owns the compiled state)
    for (auto &[id, shader] : shaderMap) {
        SDL_ReleaseGPUShader(_device, shader);
    }

    _initialized = true;
    core::Logger::instance().info("Renderer: GPU device initialized (driver: {})",
                                  SDL_GetGPUDeviceDriver(_device));
    return true;
}

// ---------------------------------------------------------------------------

void Renderer::shutdown() {
    if (!_initialized && !_device) return;

    if (_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(_device, _pipeline);
        _pipeline = nullptr;
    }
    if (_vertexBuffer) {
        SDL_ReleaseGPUBuffer(_device, _vertexBuffer);
        _vertexBuffer = nullptr;
    }

    if (_window) {
        SDL_ReleaseWindowFromGPUDevice(_device, _window);
    }
    SDL_DestroyGPUDevice(_device);
    _device = nullptr;
    _window = nullptr;
    _initialized = false;
}

// ---------------------------------------------------------------------------

void Renderer::render() {
    if (!_initialized) return;

    SDL_GPUCommandBuffer *cmdBuf = SDL_AcquireGPUCommandBuffer(_device);
    if (!cmdBuf) {
        core::Logger::instance().error(
            "Renderer: AcquireGPUCommandBuffer failed: {}", SDL_GetError());
        return;
    }

    SDL_GPUTexture *swapchain = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmdBuf, _window, &swapchain,
                                               nullptr, nullptr)) {
        SDL_CancelGPUCommandBuffer(cmdBuf);
        return;
    }

    int w = 0, h = 0;
    SDL_GetWindowSize(_window, &w, &h);

    SDL_GPUColorTargetInfo colorTarget{};
    colorTarget.texture = swapchain;
    colorTarget.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass *pass =
        SDL_BeginGPURenderPass(cmdBuf, &colorTarget, 1, nullptr);

    SDL_BindGPUGraphicsPipeline(pass, _pipeline);

    SDL_GPUViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.w = static_cast<float>(w);
    viewport.h = static_cast<float>(h);
    viewport.min_depth = 0.0f;
    viewport.max_depth = 1.0f;
    SDL_SetGPUViewport(pass, &viewport);

    SDL_GPUBufferBinding vertexBinding{};
    vertexBinding.buffer = _vertexBuffer;
    vertexBinding.offset = 0;
    SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);

    SDL_EndGPURenderPass(pass);

    SDL_SubmitGPUCommandBuffer(cmdBuf);
}

} // namespace noix::video
