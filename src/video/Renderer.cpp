#include "video/Renderer.h"
#include "core/Logger.h"
#include "core/NamespacedId.h"
#include "runtime/AssetManager.h"
#include "video/Image.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

namespace noix::video {

// --- Builtin checkerboard texture (2x2 white/gray) ---

static SurfaceRef createBuiltinCheckerboard() {
    // R8G8B8A8_UNORM on x86 LE: byte0=R, byte1=G, byte2=B, byte3=A
    uint32_t pixels[4] = {
        0xFFFFFFFF, // white
        0xFFA0A0A0, // gray
        0xFFA0A0A0, // gray
        0xFFFFFFFF  // white
    };
    SDL_Surface *surface = SDL_CreateSurfaceFrom(
        2, 2, SDL_PIXELFORMAT_ABGR8888,
        pixels, 2 * 4);
    if (!surface) return nullptr;
    return SurfaceRef(surface, [](SDL_Surface *s) { if (s) SDL_DestroySurface(s); });
}

// ---------------------------------------------------------------------------

bool Renderer::init(SDL_Window *window, runtime::AssetManager &assetMgr) {
    _window = window;
    _assetMgr = &assetMgr;

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

    SDL_GPUTextureFormat swapchainFmt =
        SDL_GetGPUSwapchainTextureFormat(_device, _window);

    // Register builtin IDs
    _pipelineCache.addBuiltin(core::NamespacedId("noix", "pipelines/textured.json"));
    _meshCache.addBuiltin(core::NamespacedId("noix", "geometry/quad.nxmd"));

    // Load builtin pipeline
    _defaultPipeline = _pipelineCache.create(
        core::NamespacedId("noix", "pipelines/textured.json"),
        _device, assetMgr, swapchainFmt);
    if (!_defaultPipeline.isValid()) {
        core::Logger::instance().error(
            "Renderer: Failed to create builtin pipeline");
        shutdown();
        return false;
    }

    // Load builtin texture (2x2 checkerboard, code-generated)
    auto checkerboard = createBuiltinCheckerboard();
    if (!checkerboard) {
        core::Logger::instance().error(
            "Renderer: Failed to create builtin checkerboard surface");
        shutdown();
        return false;
    }
    _defaultTexture = Texture::resolve(
        core::NamespacedId("noix", "builtin-default"),
        checkerboard, "",
        core::ResourceMode::Dynamic);
    if (!_defaultTexture.isValid()) {
        core::Logger::instance().error(
            "Renderer: Failed to create builtin texture");
        shutdown();
        return false;
    }

    // Load builtin mesh (unit quad)
    _defaultMesh = _meshCache.create(
        core::NamespacedId("noix", "geometry/quad.nxmd"),
        _device, assetMgr);
    if (!_defaultMesh.isValid()) {
        core::Logger::instance().error(
            "Renderer: Failed to create builtin mesh");
        shutdown();
        return false;
    }

    // Load default material
    _defaultMaterial = _materialCache.create(
        core::NamespacedId("noix", "materials/brick.json"), assetMgr);

    // Set up transform matrices: identity view + identity projection (NDC geometry)
    _view = glm::mat4(1.0f);
    _proj = glm::mat4(1.0f);

    _initialized = true;
    core::Logger::instance().info("Renderer: GPU device initialized (driver: {})",
                                  SDL_GetGPUDeviceDriver(_device));
    return true;
}

// ---------------------------------------------------------------------------

void Renderer::shutdown() {
    if (!_initialized && !_device) return;

    // Release GPU resources BEFORE destroying the device.
    // Texture/Image destructors call Application::renderer().gpuDevice().
    _materialCache = MaterialCache();
    _meshCache = MeshCache();
    _pipelineCache = PipelineCache();
    Texture::slotMap().clear();
    Image::slotMap().clear();

    if (_window) {
        SDL_ReleaseWindowFromGPUDevice(_device, _window);
    }
    SDL_DestroyGPUDevice(_device);
    _device = nullptr;
    _window = nullptr;
    _initialized = false;
}

// ---------------------------------------------------------------------------

void Renderer::updateResources(
    const std::vector<core::NamespacedId> &pipelineIds,
    const std::vector<core::NamespacedId> &meshIds,
    const std::vector<core::NamespacedId> &materialIds) {
    if (!_initialized) return;

    SDL_GPUTextureFormat swapchainFmt =
        SDL_GetGPUSwapchainTextureFormat(_device, _window);

    _pipelineCache.update(pipelineIds, _device, *_assetMgr, swapchainFmt);
    _meshCache.update(meshIds, _device, *_assetMgr);
    _materialCache.update(materialIds, *_assetMgr);
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

    SDL_BindGPUGraphicsPipeline(pass,
                                _pipelineCache.get(_defaultPipeline));

    SDL_GPUViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.w = static_cast<float>(w);
    viewport.h = static_cast<float>(h);
    viewport.min_depth = 0.0f;
    viewport.max_depth = 1.0f;
    SDL_SetGPUViewport(pass, &viewport);

    // Push transform uniforms (std140 aligned)
    struct { glm::mat4 view; glm::mat4 proj; } transformData;
    transformData.view = _view;
    transformData.proj = _proj;
    SDL_PushGPUVertexUniformData(cmdBuf, 0, &transformData,
                                 sizeof(transformData));

    GeometryDef *geom = _meshCache.get(_defaultMesh);
    if (geom) {
        SDL_GPUBufferBinding vertexBinding{};
        vertexBinding.buffer = geom->vertexBuffer();
        vertexBinding.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
    }

    // Bind texture sampler (builtin checkerboard)
    Texture *tex = _defaultTexture.get();
    if (tex && tex->gpuTexture() && tex->gpuSampler()) {
        SDL_GPUTextureSamplerBinding bind{};
        bind.texture = tex->gpuTexture();
        bind.sampler = tex->gpuSampler();
        SDL_BindGPUFragmentSamplers(pass, 0, &bind, 1);
    }

    if (geom) {
        SDL_DrawGPUPrimitives(pass, geom->indexCount(), 1, 0, 0);
    }

    SDL_EndGPURenderPass(pass);

    SDL_SubmitGPUCommandBuffer(cmdBuf);
}

} // namespace noix::video
