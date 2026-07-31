#include "video/Renderer.h"
#include "core/Logger.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

namespace noix::video {

bool Renderer::init(SDL_Window* window) {
    _window = window;

    _device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
    if (!_device) {
        core::Logger::instance().error("Renderer: SDL_CreateGPUDevice failed: {}", SDL_GetError());
        return false;
    }

    if (!SDL_ClaimWindowForGPUDevice(_device, _window)) {
        core::Logger::instance().error("Renderer: SDL_ClaimWindowForGPUDevice failed: {}", SDL_GetError());
        SDL_DestroyGPUDevice(_device);
        _device = nullptr;
        return false;
    }

    _initialized = true;
    core::Logger::instance().info("Renderer: GPU device initialized (driver: {})",
                                  SDL_GetGPUDeviceDriver(_device));
    return true;
}

void Renderer::shutdown() {
    if (!_initialized) return;
    SDL_ReleaseWindowFromGPUDevice(_device, _window);
    SDL_DestroyGPUDevice(_device);
    _device = nullptr;
    _window = nullptr;
    _initialized = false;
}

void Renderer::render() {
    if (!_initialized) return;

    SDL_GPUCommandBuffer* cmdBuf = SDL_AcquireGPUCommandBuffer(_device);
    if (!cmdBuf) {
        core::Logger::instance().error("Renderer: AcquireGPUCommandBuffer failed: {}", SDL_GetError());
        return;
    }

    SDL_GPUTexture* swapchain = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmdBuf, _window, &swapchain, nullptr, nullptr)) {
        /* Window minimized or swapchain unavailable — cancel and skip */
        SDL_CancelGPUCommandBuffer(cmdBuf);
        return;
    }

    /* Clear color: (0.2, 0.3, 0.3, 1.0) */
    SDL_GPUColorTargetInfo colorTarget{};
    colorTarget.texture = swapchain;
    colorTarget.clear_color = {0.2f, 0.3f, 0.3f, 1.0f};
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmdBuf, &colorTarget, 1, nullptr);
    SDL_EndGPURenderPass(pass);

    SDL_SubmitGPUCommandBuffer(cmdBuf);
}

} // namespace noix::video
