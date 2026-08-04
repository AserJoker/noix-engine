#pragma once

/*
 * Renderer — SDL3 GPU rendering backend.
 * Loads pipeline definitions and materials from JSON resources and renders geometry.
 */

#include "video/GeometryDef.h"
#include "video/MaterialDef.h"

#include <SDL3/SDL_gpu.h>

#include <map>
#include <string>

namespace noix::runtime { class AssetManager; }
namespace noix::core { class NamespacedId; }

namespace noix::video {

class Renderer {
public:
  Renderer() = default;
  virtual ~Renderer() = default;

  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;

  /// Initialize GPU device, claim the window, and load the default scene.
  bool init(SDL_Window *window, runtime::AssetManager &assetMgr);

  /// Shut down and release GPU resources.
  void shutdown();

  /// Render the frame.
  void render();

private:
  SDL_GPUDevice *_device = nullptr;
  SDL_Window *_window = nullptr;
  bool _initialized = false;

  GeometryDef _geometry;
  SDL_GPUGraphicsPipeline *_pipeline = nullptr;
  SDL_GPUTexture *_texture = nullptr;
  SDL_GPUSampler *_sampler = nullptr;

  SDL_GPUShader *loadShader(const std::string &absolutePath,
                            SDL_GPUShaderStage stage);
  SDL_GPUTexture *loadTexture(const std::string &absolutePath,
                               SDL_GPUTextureFormat textureFormat);
};

} // namespace noix::video
