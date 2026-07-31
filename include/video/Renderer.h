#pragma once

/*
 * Renderer — SDL3 GPU rendering backend.
 * Currently only clears the window with a configurable color.
 */

#include <SDL3/SDL_gpu.h>

namespace noix::video {

class Renderer {
public:
  Renderer() = default;
  virtual ~Renderer() = default;

  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;

  /// Initialize GPU device and claim the window.
  bool init(SDL_Window *window);

  /// Shut down and release GPU resources.
  void shutdown();

  /// Clear the window with the configured clear color and present.
  void render();

private:
  SDL_GPUDevice *_device = nullptr;
  SDL_Window *_window = nullptr;
  bool _initialized = false;
};

} // namespace noix::video
