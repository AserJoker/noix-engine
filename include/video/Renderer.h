#pragma once

/*
 * Renderer — SDL3 GPU rendering backend.
 * Iterates Drawable list, binds resources, issues draw calls.
 */

#include "video/Drawable.h"
#include "video/Pipeline.h"
#include "video/Texture.h"

#include <SDL3/SDL_gpu.h>
#include <glm/mat4x4.hpp>

#include <vector>

namespace noix::runtime { class AssetManager; }

namespace noix::video {

class Renderer {
public:
  Renderer() = default;
  virtual ~Renderer() = default;

  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;

  /// Initialize GPU device, claim the window, and load builtin resources.
  bool init(SDL_Window *window, runtime::AssetManager &assetMgr);

  /// Shut down and release all GPU resources.
  void shutdown();

  /// Render the frame.
  void render();

  SDL_GPUDevice *gpuDevice() const { return _device; }

  /// Add a Drawable to the render list.
  void addDrawable(Drawable drawable);

  /// Remove all Drawables.
  void clearDrawables();

private:
  SDL_GPUDevice *_device = nullptr;
  SDL_Window *_window = nullptr;
  runtime::AssetManager *_assetMgr = nullptr;
  bool _initialized = false;

  glm::mat4 _view{1.0f};
  glm::mat4 _proj{1.0f};

  Pipeline::Handle _defaultPipeline;
  Texture::Handle  _defaultTexture;

  std::vector<Drawable> _drawables;
};

} // namespace noix::video
