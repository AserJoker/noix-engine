#include "video/Renderer.h"
#include "video/BuiltinShaders.h"
#include "core/Logger.h"
#include "core/NamespacedId.h"
#include "runtime/AssetManager.h"
#include "video/Drawable.h"
#include "video/Image.h"
#include "video/Mesh.h"
#include "video/Pipeline.h"
#include "video/Shader.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

namespace noix::video {

// --- Builtin checkerboard texture (2x2 white/gray) ---

static SurfaceRef createBuiltinCheckerboard() {
    SDL_Surface *surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_ABGR8888);
    if (!surface) return nullptr;
    auto *pixels = static_cast<uint32_t *>(surface->pixels);
    pixels[0] = 0xFFFFFFFF; // white
    pixels[1] = 0xFFA0A0A0; // gray
    pixels[2] = 0xFFA0A0A0; // gray
    pixels[3] = 0xFFFFFFFF; // white
    return SurfaceRef(surface, [](SDL_Surface *s) { if (s) SDL_DestroySurface(s); });
}

// --- Builtin pipeline JSON definition ---

static const char kBuiltinPipelineTextured[] = R"({
  "vertex_shader": "noix:builtin-unlit-vert",
  "fragment_shader": "noix:builtin-unlit-frag",
  "primitive_type": "triangle_list",
  "vertex_input": {
    "buffers": [
      {"slot": 0, "stride": 16, "input_rate": "vertex"}
    ],
    "attributes": [
      {"location": 0, "buffer_slot": 0, "format": "float2", "offset": 0},
      {"location": 1, "buffer_slot": 0, "format": "float2", "offset": 8}
    ]
  },
  "color_targets": [
    {
      "format": "swapchain",
      "blend": {
        "src_color": "src_alpha",
        "dst_color": "one_minus_src_alpha",
        "color_op": "add",
        "src_alpha": "one",
        "dst_alpha": "one_minus_src_alpha",
        "alpha_op": "add",
        "color_write_mask": 15,
        "enable_blend": true
      }
    }
  ]
})";

/// Register embedded SPIR-V data for builtin shaders into AssetManager.
static void registerBuiltinShaders(runtime::AssetManager &assetMgr) {
    auto vertSpirv = std::make_shared<std::vector<uint8_t>>(
        kShader_unlit_texture_vert_spv,
        kShader_unlit_texture_vert_spv + kShader_unlit_texture_vert_spv_size);
    assetMgr.create<Shader>(
        core::NamespacedId("noix", "builtin-unlit-vert"),
        vertSpirv);
    assetMgr.addBuiltin(core::NamespacedId("noix", "builtin-unlit-vert"));

    auto fragSpirv = std::make_shared<std::vector<uint8_t>>(
        kShader_unlit_texture_frag_spv,
        kShader_unlit_texture_frag_spv + kShader_unlit_texture_frag_spv_size);
    assetMgr.create<Shader>(
        core::NamespacedId("noix", "builtin-unlit-frag"),
        fragSpirv);
    assetMgr.addBuiltin(core::NamespacedId("noix", "builtin-unlit-frag"));
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

    SDL_GPUTextureFormat swapchainFormat =
        SDL_GetGPUSwapchainTextureFormat(_device, _window);

    // Register builtin IDs
    assetMgr.addBuiltin(core::NamespacedId("noix", "geometry/quad.nxmd"));

    // Register embedded builtin shader SPIR-V data
    registerBuiltinShaders(assetMgr);

    // Build builtin pipeline directly from embedded JSON
    core::NamespacedId pipelineId("noix", "builtin-unlit-pipeline");
    auto jsonBegin = reinterpret_cast<const uint8_t *>(kBuiltinPipelineTextured);
    auto jsonEnd = jsonBegin + sizeof(kBuiltinPipelineTextured) - 1;
    _defaultPipeline = assetMgr.create<Pipeline>(
        pipelineId,
        std::vector<uint8_t>(jsonBegin, jsonEnd),
        swapchainFormat);
    assetMgr.addBuiltin(pipelineId);
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
    _defaultTexture = assetMgr.create<Texture>(
        core::NamespacedId("noix", "builtin-default"),
        checkerboard,
        std::nullopt,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_FILTER_NEAREST);
    if (!_defaultTexture.isValid()) {
        core::Logger::instance().error(
            "Renderer: Failed to create builtin texture");
        shutdown();
        return false;
    }

    // Load builtin mesh (unit quad)
    auto builtinMesh = assetMgr.create<Mesh>(
        core::NamespacedId("noix", "geometry/quad.nxmd"));
    if (!builtinMesh.isValid()) {
        core::Logger::instance().error(
            "Renderer: Failed to create builtin mesh");
        shutdown();
        return false;
    }

    // Load default material
    auto builtinMaterial = assetMgr.load<Material>(
        core::NamespacedId("noix", "materials/brick.json"));

    // Create default Drawable for validation
    if (builtinMesh.isValid() && builtinMaterial.isValid()) {
        addDrawable(Drawable(std::move(builtinMesh), std::move(builtinMaterial)));
    }

    // Set up transform matrices: identity view + orthographic projection
    _view = glm::mat4(1.0f);
    _proj = glm::mat4(1.0f); // Updated per-frame in render()

    _initialized = true;
    core::Logger::instance().info("Renderer: GPU device initialized (driver: {})",
                                  SDL_GetGPUDeviceDriver(_device));
    return true;
}

// ---------------------------------------------------------------------------

void Renderer::shutdown() {
    if (!_initialized && !_device) return;

    _drawables.clear();

    // Resources are released by AssetManager destruction (which happens before
    // Renderer destruction in Application::cleanup).

    if (_window) {
        SDL_ReleaseWindowFromGPUDevice(_device, _window);
    }
    SDL_DestroyGPUDevice(_device);
    _device = nullptr;
    _window = nullptr;
    _initialized = false;
}

// ---------------------------------------------------------------------------

void Renderer::addDrawable(Drawable drawable) {
    _drawables.push_back(std::move(drawable));
}

void Renderer::clearDrawables() {
    _drawables.clear();
}

// ---------------------------------------------------------------------------

void Renderer::render() {
    if (!_initialized) return;

    // TEST: rotate first drawable over time
    if (!_drawables.empty()) {
        float seconds = static_cast<float>(SDL_GetTicks()) / 1000.0f;
        float angle = seconds * 0.5f; // 0.5 rad/s
        _drawables[0].transform() = glm::rotate(glm::mat4(1.0f), angle,
                                                  glm::vec3(0.0f, 0.0f, 1.0f));
    }

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

    // Update orthographic projection to correct aspect ratio
    float aspect = static_cast<float>(w) / static_cast<float>(h);
    _proj = glm::ortho(-aspect, aspect, -1.0f, 1.0f);

    SDL_GPUColorTargetInfo colorTarget{};
    colorTarget.texture = swapchain;
    colorTarget.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass *pass =
        SDL_BeginGPURenderPass(cmdBuf, &colorTarget, 1, nullptr);

    // Bind builtin pipeline
    Pipeline *pipeline = _defaultPipeline.get();
    if (!pipeline || !pipeline->gpuPipeline()) {
        SDL_EndGPURenderPass(pass);
        SDL_SubmitGPUCommandBuffer(cmdBuf);
        return;
    }
    SDL_BindGPUGraphicsPipeline(pass, pipeline->gpuPipeline());

    SDL_GPUViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.w = static_cast<float>(w);
    viewport.h = static_cast<float>(h);
    viewport.min_depth = 0.0f;
    viewport.max_depth = 1.0f;
    SDL_SetGPUViewport(pass, &viewport);

    // Render each Drawable
    for (auto &drawable : _drawables) {
        Mesh *mesh = drawable.mesh().get();
        Material *material = drawable.material().get();
        if (!mesh || !material) continue;

        // Push per-object vertex uniforms: { model, view, proj }
        struct { glm::mat4 model; glm::mat4 view; glm::mat4 proj; } transformData;
        transformData.model = drawable.transform();
        transformData.view = _view;
        transformData.proj = _proj;
        SDL_PushGPUVertexUniformData(cmdBuf, 0, &transformData,
                                     sizeof(transformData));

        // Bind mesh vertex/index buffers
        SDL_GPUBufferBinding vertexBinding{};
        vertexBinding.buffer = mesh->vertexBuffer();
        vertexBinding.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);

        if (mesh->indexBuffer()) {
            SDL_GPUBufferBinding indexBinding{};
            indexBinding.buffer = mesh->indexBuffer();
            indexBinding.offset = 0;
            SDL_BindGPUIndexBuffer(pass, &indexBinding, mesh->indexType());
        }

        // Bind texture: use material's texture if available, else fallback checkerboard
        Texture *tex = _defaultTexture.get();
        auto payload = material->data();
        if (payload && !payload->textures.empty()) {
            // TODO: load texture from TextureBinding.asset via AssetManager
            // For now, still use default checkerboard
        }
        if (tex && tex->gpuTexture() && tex->gpuSampler()) {
            SDL_GPUTextureSamplerBinding bind{};
            bind.texture = tex->gpuTexture();
            bind.sampler = tex->gpuSampler();
            SDL_BindGPUFragmentSamplers(pass, 0, &bind, 1);
        }

        // Draw
        if (mesh->indexBuffer()) {
            SDL_DrawGPUIndexedPrimitives(pass, mesh->indexCount(), 1, 0, 0, 0);
        } else {
            SDL_DrawGPUPrimitives(pass, mesh->vertexCount(), 1, 0, 0);
        }
    }

    SDL_EndGPURenderPass(pass);

    SDL_SubmitGPUCommandBuffer(cmdBuf);
}

} // namespace noix::video
