#pragma once

/*
 * Renderer — SDL3 GPU rendering backend.
 * Manages GPU resources via Pipeline, Texture, MeshCache, MaterialCache.
 */

#include "video/MaterialCache.h"
#include "video/MeshCache.h"
#include "video/Pipeline.h"
#include "video/Texture.h"

#include <SDL3/SDL_gpu.h>
#include <glm/mat4x4.hpp>

#include <vector>

namespace noix::runtime { class AssetManager; }
namespace noix::core { class NamespacedId; }

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

  /// Batch diff-update resource caches.
  void updateResources(const std::vector<core::NamespacedId> &meshIds,
                       const std::vector<core::NamespacedId> &materialIds);

  MeshCache &meshCache() { return _meshCache; }
  MaterialCache &materialCache() { return _materialCache; }

  SDL_GPUDevice *gpuDevice() const { return _device; }

private:
  SDL_GPUDevice *_device = nullptr;
  SDL_Window *_window = nullptr;
  runtime::AssetManager *_assetMgr = nullptr;
  bool _initialized = false;

  MeshCache _meshCache;
  MaterialCache _materialCache;

  glm::mat4 _view{1.0f};
  glm::mat4 _proj{1.0f};

  Pipeline::Handle _defaultPipeline;
  ResourceHandle _defaultMesh;
  ResourceHandle _defaultMaterial;

  Texture::Handle _defaultTexture;
};

} // namespace noix::video
