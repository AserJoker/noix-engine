#pragma once

/*
 * PipelineDef — Serializable pipeline definition.
 * Mirrors SDL_GPUGraphicsPipelineCreateInfo fields using SDL3 types directly.
 * JSON serialization via string ↔ SDL enum maps.
 */

#include "core/NamespacedId.h"
#include "core/Value.h"

#include <SDL3/SDL_gpu.h>

#include <optional>
#include <string>
#include <vector>

namespace noix::video {

// ---- SDL enum ↔ string mappings ----

SDL_GPUPrimitiveType toSDLPrimitiveType(const std::string &s);
std::string fromSDLPrimitiveType(SDL_GPUPrimitiveType t);

SDL_GPUVertexElementFormat toSDLVertexFormat(const std::string &s);
std::string fromSDLVertexFormat(SDL_GPUVertexElementFormat f);

SDL_GPUVertexInputRate toSDLVertexInputRate(const std::string &s);
std::string fromSDLVertexInputRate(SDL_GPUVertexInputRate r);

SDL_GPUFillMode toSDLFillMode(const std::string &s);
std::string fromSDLFillMode(SDL_GPUFillMode m);

SDL_GPUCullMode toSDLCullMode(const std::string &s);
std::string fromSDLCullMode(SDL_GPUCullMode m);

SDL_GPUFrontFace toSDLFrontFace(const std::string &s);
std::string fromSDLFrontFace(SDL_GPUFrontFace f);

SDL_GPUCompareOp toSDLCompareOp(const std::string &s);
std::string fromSDLCompareOp(SDL_GPUCompareOp op);

SDL_GPUStencilOp toSDLStencilOp(const std::string &s);
std::string fromSDLStencilOp(SDL_GPUStencilOp op);

SDL_GPUBlendFactor toSDLBlendFactor(const std::string &s);
std::string fromSDLBlendFactor(SDL_GPUBlendFactor f);

SDL_GPUBlendOp toSDLBlendOp(const std::string &s);
std::string fromSDLBlendOp(SDL_GPUBlendOp op);

SDL_GPUTextureFormat toSDLTextureFormat(const std::string &s);
std::string fromSDLTextureFormat(SDL_GPUTextureFormat f);

SDL_GPUSampleCount toSDLSampleCount(int n);
int fromSDLSampleCount(SDL_GPUSampleCount sc);

// ---- Sub-structs for PipelineDef ----
// Thin wrappers that add a "swapchain" format marker where SDL structs don't have one.

struct StencilOpDef {
    SDL_GPUStencilOp failOp = SDL_GPU_STENCILOP_KEEP;
    SDL_GPUStencilOp passOp = SDL_GPU_STENCILOP_KEEP;
    SDL_GPUStencilOp depthFailOp = SDL_GPU_STENCILOP_KEEP;
    SDL_GPUCompareOp compareOp = SDL_GPU_COMPAREOP_ALWAYS;
};

struct DepthStencilDef {
    SDL_GPUCompareOp compareOp = SDL_GPU_COMPAREOP_LESS;
    StencilOpDef backStencil;
    StencilOpDef frontStencil;
    Uint8 compareMask = 0xFF;
    Uint8 writeMask = 0xFF;
    bool enableDepthTest = true;
    bool enableDepthWrite = true;
    bool enableStencilTest = false;
};

struct BlendDef {
    SDL_GPUBlendFactor srcColor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    SDL_GPUBlendFactor dstColor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    SDL_GPUBlendOp colorOp = SDL_GPU_BLENDOP_ADD;
    SDL_GPUBlendFactor srcAlpha = SDL_GPU_BLENDFACTOR_ONE;
    SDL_GPUBlendFactor dstAlpha = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    SDL_GPUBlendOp alphaOp = SDL_GPU_BLENDOP_ADD;
    SDL_GPUColorComponentFlags colorWriteMask = 0xF; // RGBA all enabled
    bool enableBlend = true;
    bool enableColorWriteMask = false;
};

struct ColorTargetDef {
    std::string format = "swapchain";
    std::optional<BlendDef> blend;
};

struct RasterizerDef {
    SDL_GPUFillMode fillMode = SDL_GPU_FILLMODE_FILL;
    SDL_GPUCullMode cullMode = SDL_GPU_CULLMODE_NONE;
    SDL_GPUFrontFace frontFace = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    float depthBiasConstantFactor = 0.0f;
    float depthBiasClamp = 0.0f;
    float depthBiasSlopeFactor = 0.0f;
    bool enableDepthBias = false;
    bool enableDepthClip = true;
};

struct MultisampleDef {
    SDL_GPUSampleCount sampleCount = SDL_GPU_SAMPLECOUNT_1;
    Uint32 sampleMask = 0;
    bool enableAlphaToCoverage = false;
};

// ---- PipelineDef ----

struct PipelineDef {
    core::NamespacedId vertexShader;
    core::NamespacedId fragmentShader;

    SDL_GPUPrimitiveType primitiveType = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    std::vector<SDL_GPUVertexBufferDescription> vertexBuffers;
    std::vector<SDL_GPUVertexAttribute> vertexAttributes;

    RasterizerDef rasterizer;

    std::optional<DepthStencilDef> depthStencil;

    std::vector<ColorTargetDef> colorTargets;

    MultisampleDef multisample;

    /// Parse from a Value (JSON). Returns nullopt on failure.
    static std::optional<PipelineDef> parse(const core::Value &v);

    /// Serialize to Value (JSON).
    core::Value dump() const;

    /// Create an SDL_GPUGraphicsPipeline from this definition.
    SDL_GPUGraphicsPipeline *createPipeline(
        SDL_GPUDevice *device,
        const std::map<core::NamespacedId, SDL_GPUShader *> &shaderMap,
        SDL_GPUTextureFormat swapchainFormat) const;
};

} // namespace noix::video
