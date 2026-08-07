#pragma once

/*
 * RenderGraph — defines the rendering pass sequence and intermediate textures.
 * Each pass maps to a Pipeline and a render target (swapchain or offscreen texture).
 * Material declares per-pass resources keyed by pass name; RenderGraph decides
 * which pipeline and target each pass uses.
 */

#include "core/NamespacedId.h"

#include <SDL3/SDL_gpu.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace noix::video {

// ---- Intermediate texture declaration ----

struct RenderTextureDef {
    std::string name;
    SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    enum SizeMode { Window, Fixed } sizeMode = Window;
    uint32_t fixedWidth = 0;
    uint32_t fixedHeight = 0;
};

// ---- Pass definition ----

struct RenderPassDef {
    std::string name;
    core::NamespacedId pipeline;
    std::string target = "swapchain";  // "swapchain" or name from RenderTextureDef
    enum Sort { None, BackToFront, FrontToBack } sort = None;
};

// ---- RenderGraph ----

class RenderGraph {
public:
    /// Parse a RenderGraph from a JSON file.
    static std::optional<RenderGraph> fromFile(const std::filesystem::path &path);

    const std::vector<RenderPassDef> &passes() const { return _passes; }
    const std::vector<RenderTextureDef> &textures() const { return _textures; }

    /// Add a pass definition (for programmatic/builtin construction).
    void addPass(RenderPassDef pass) { _passes.push_back(std::move(pass)); }

    /// Add an intermediate texture definition.
    void addTexture(RenderTextureDef tex) { _textures.push_back(std::move(tex)); }

private:
    std::vector<RenderPassDef> _passes;
    std::vector<RenderTextureDef> _textures;
};

} // namespace noix::video
