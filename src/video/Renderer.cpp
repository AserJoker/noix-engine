#include "video/Renderer.h"
#include "video/PipelineDef.h"
#include "video/MaterialDef.h"
#include "core/Logger.h"
#include "runtime/AssetManager.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <fstream>
#include <sstream>

namespace noix::video {

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
    createInfo.num_samplers = (stage == SDL_GPU_SHADERSTAGE_FRAGMENT) ? 1 : 0;

    SDL_GPUShader *shader = SDL_CreateGPUShader(_device, &createInfo);
    SDL_free(code);

    if (!shader) {
        core::Logger::instance().error(
            "Renderer: Failed to create shader from: {}", absolutePath);
    }
    return shader;
}

SDL_GPUTexture *Renderer::loadTexture(const std::string &absolutePath) {
    SDL_Surface *surface = IMG_Load(absolutePath.c_str());
    if (!surface) {
        core::Logger::instance().error("Renderer: Failed to load image: {}", SDL_GetError());
        return nullptr;
    }

    // Texture format is R8G8B8A8_UNORM — the canonical format.
    // On x86 (little-endian), SDL_PIXELFORMAT_ABGR8888 has memory layout R,G,B,A
    // which matches R8G8B8A8_UNORM. The shader handles channel swap via a uniform
    // when the swapchain is BGR.
    if (surface->format != SDL_PIXELFORMAT_ABGR8888) {
        SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ABGR8888);
        SDL_DestroySurface(surface);
        if (!converted) {
            core::Logger::instance().error("Renderer: Failed to convert surface to ABGR8888");
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

    SDL_GPUTexture *texture = SDL_CreateGPUTexture(_device, &texInfo);
    if (!texture) {
        core::Logger::instance().error("Renderer: Failed to create texture: {}",
                                       SDL_GetError());
        SDL_DestroySurface(surface);
        return nullptr;
    }
    // Upload pixel data via transfer buffer
    size_t dataSize = static_cast<size_t>(surface->pitch) * surface->h;
    SDL_GPUTransferBufferCreateInfo xferInfo{};
    xferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    xferInfo.size = static_cast<Uint32>(dataSize);
    xferInfo.props = 0;
    SDL_GPUTransferBuffer *xferBuf = SDL_CreateGPUTransferBuffer(_device, &xferInfo);
    if (!xferBuf) {
        core::Logger::instance().error("Renderer: Failed to create transfer buffer");
        SDL_DestroySurface(surface);
        SDL_ReleaseGPUTexture(_device, texture);
        return nullptr;
    }

    void *mapped = SDL_MapGPUTransferBuffer(_device, xferBuf, false);
    if (!mapped) {
        core::Logger::instance().error("Renderer: Failed to map transfer buffer");
        SDL_ReleaseGPUTransferBuffer(_device, xferBuf);
        SDL_DestroySurface(surface);
        SDL_ReleaseGPUTexture(_device, texture);
        return nullptr;
    }
    SDL_memcpy(mapped, surface->pixels, dataSize);
    SDL_UnmapGPUTransferBuffer(_device, xferBuf);

    SDL_GPUCommandBuffer *uploadCmd = SDL_AcquireGPUCommandBuffer(_device);
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
    SDL_ReleaseGPUTransferBuffer(_device, xferBuf);
    SDL_DestroySurface(surface);

    return texture;
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

    // --- Load material ---
    auto matPath = assetMgr.resolve(core::NamespacedId("noix", "materials/brick.json"));
    if (!matPath.has_value()) {
        core::Logger::instance().error("Renderer: Material not found: noix:materials/brick.json");
        shutdown();
        return false;
    }
    auto material = MaterialDef::load(matPath->string());
    if (!material.has_value()) {
        core::Logger::instance().error("Renderer: Failed to parse material");
        shutdown();
        return false;
    }

    // --- Load geometry from NXMD ---
    auto geomPath = assetMgr.resolve(core::NamespacedId("noix", "geometry/quad.nxmd"));
    if (!geomPath.has_value()) {
        core::Logger::instance().error(
            "Renderer: Geometry not found: noix:geometry/quad.nxmd");
        shutdown();
        return false;
    }
    auto nxmdData = NxmdData::load(geomPath->string());
    if (!nxmdData.has_value()) {
        core::Logger::instance().error("Renderer: Failed to parse geometry");
        shutdown();
        return false;
    }

    auto geom = GeometryDef::create(_device, *nxmdData);
    if (!geom.has_value()) {
        core::Logger::instance().error("Renderer: Failed to create geometry GPU resources");
        shutdown();
        return false;
    }
    _geometry = std::move(*geom);

    // --- Load texture ---
    if (!material->textures().empty()) {
        const auto &texBinding = material->textures().begin()->second;
        auto texPath = assetMgr.resolve(texBinding.asset);
        if (!texPath.has_value()) {
            core::Logger::instance().error(
                "Renderer: Texture not found: {}", texBinding.asset.toString());
            shutdown();
            return false;
        }
        _texture = loadTexture(texPath->string());
        if (!_texture) {
            shutdown();
            return false;
        }
    }

    // --- Load pipeline ---
    auto pipelinePath = assetMgr.resolve(material->pipeline());
    if (!pipelinePath.has_value()) {
        core::Logger::instance().error(
            "Renderer: Pipeline not found: {}", material->pipeline().toString());
        shutdown();
        return false;
    }

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
        core::Logger::instance().error("Renderer: Failed to parse pipeline definition");
        shutdown();
        return false;
    }
    // Load shaders
    std::map<core::NamespacedId, SDL_GPUShader *> shaderMap;
    auto vsPath = assetMgr.resolve(pipelineDef->vertexShader());
    if (!vsPath.has_value()) {
        core::Logger::instance().error(
            "Renderer: Vertex shader not found: {}", pipelineDef->vertexShader().toString());
        shutdown();
        return false;
    }
    SDL_GPUShader *vs = loadShader(vsPath->string(), SDL_GPU_SHADERSTAGE_VERTEX);
    if (!vs) { shutdown(); return false; }
    shaderMap[pipelineDef->vertexShader()] = vs;

    if (pipelineDef->fragmentShader().has_value()) {
        auto fsPath = assetMgr.resolve(*pipelineDef->fragmentShader());
        if (!fsPath.has_value()) {
            core::Logger::instance().error(
                "Renderer: Fragment shader not found: {}", pipelineDef->fragmentShader()->toString());
            shutdown();
            return false;
        }
        SDL_GPUShader *fs = loadShader(fsPath->string(), SDL_GPU_SHADERSTAGE_FRAGMENT);
        if (!fs) { shutdown(); return false; }
        shaderMap[*pipelineDef->fragmentShader()] = fs;
    }

    _pipeline = pipelineDef->createPipeline(_device, shaderMap,
        SDL_GetGPUSwapchainTextureFormat(_device, _window));
    if (!_pipeline) {
        core::Logger::instance().error(
            "Renderer: Failed to create graphics pipeline: {}", SDL_GetError());
        shutdown();
        return false;
    }

    // Release shader objects
    for (auto &[id, shader] : shaderMap) {
        SDL_ReleaseGPUShader(_device, shader);
    }

    // Create sampler for texture binding
    if (_texture) {
        SDL_GPUSamplerCreateInfo samplerInfo{};
        samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
        samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
        samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        samplerInfo.props = 0;
        _sampler = SDL_CreateGPUSampler(_device, &samplerInfo);
        if (!_sampler) {
            core::Logger::instance().error(
                "Renderer: Failed to create sampler: {}", SDL_GetError());
            shutdown();
            return false;
        }
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
    if (_texture) {
        SDL_ReleaseGPUTexture(_device, _texture);
        _texture = nullptr;
    }
    if (_sampler) {
        SDL_ReleaseGPUSampler(_device, _sampler);
        _sampler = nullptr;
    }
    _geometry.destroy(_device);

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
    vertexBinding.buffer = _geometry.vertexBuffer();
    vertexBinding.offset = 0;
    SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);

    // Bind texture sampler
    if (_texture && _sampler) {
        SDL_GPUTextureSamplerBinding texBinding{};
        texBinding.texture = _texture;
        texBinding.sampler = _sampler;
        SDL_BindGPUFragmentSamplers(pass, 0, &texBinding, 1);
    }

    SDL_DrawGPUPrimitives(pass, _geometry.indexCount(), 1, 0, 0);

    SDL_EndGPURenderPass(pass);

    SDL_SubmitGPUCommandBuffer(cmdBuf);
}

} // namespace noix::video
