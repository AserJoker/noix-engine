#pragma once

/*
 * Renderer — SDL3 GPU rendering backend.
 * Iterates RenderGraph passes, filters Drawables by material pass participation,
 * binds resources, issues draw calls.
 */

#include "video/Drawable.h"
#include "video/Pipeline.h"
#include "video/RenderGraph.h"
#include "video/Texture.h"

#include <SDL3/SDL_gpu.h>
#include <glm/mat4x4.hpp>

#include <string>
#include <unordered_map>
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
  /// Create GPU textures for intermediate render targets declared in the RenderGraph.
  bool createRenderTextures(int windowWidth, int windowHeight);

  SDL_GPUDevice *_device = nullptr;
  SDL_Window *_window = nullptr;
  runtime::AssetManager *_assetMgr = nullptr;
  bool _initialized = false;

  glm::mat4 _view{1.0f};
  glm::mat4 _proj{1.0f};

  RenderGraph _renderGraph;
  Texture::Handle _defaultTexture;

  // Intermediate render textures: name → SDL_GPUTexture*
  std::unordered_map<std::string, SDL_GPUTexture *> _renderTextures;

  std::vector<Drawable> _drawables;
};

} // namespace noix::video
