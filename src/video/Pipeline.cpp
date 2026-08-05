#include "video/Pipeline.h"
#include "core/Logger.h"
#include "core/NamespacedId.h"
#include "core/Value.h"
#include "runtime/Application.h"
#include "runtime/AssetManager.h"
#include "video/Renderer.h"
#include "video/Shader.h"

#include <SDL3/SDL_gpu.h>

#include <unordered_map>

namespace noix::video {

// =====================================================================
// SDL enum ↔ string maps
// =====================================================================

static SDL_GPUPrimitiveType toSDLPrimitiveType(const std::string &s) {
    static const std::unordered_map<std::string, SDL_GPUPrimitiveType> m = {
        {"triangle_list",  SDL_GPU_PRIMITIVETYPE_TRIANGLELIST},
        {"triangle_strip", SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP},
        {"line_list",      SDL_GPU_PRIMITIVETYPE_LINELIST},
        {"line_strip",     SDL_GPU_PRIMITIVETYPE_LINESTRIP},
        {"point_list",     SDL_GPU_PRIMITIVETYPE_POINTLIST},
    };
    auto it = m.find(s);
    return it != m.end() ? it->second : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
}

static SDL_GPUVertexElementFormat toSDLVertexFormat(const std::string &s) {
    static const std::unordered_map<std::string, SDL_GPUVertexElementFormat> m = {
        {"float2",      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2},
        {"float3",      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3},
        {"float4",      SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4},
        {"byte2_norm",  SDL_GPU_VERTEXELEMENTFORMAT_BYTE2_NORM},
        {"byte4_norm",  SDL_GPU_VERTEXELEMENTFORMAT_BYTE4_NORM},
        {"ubyte2_norm", SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2_NORM},
        {"ubyte4_norm", SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM},
        {"short2_norm", SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM},
        {"short4_norm", SDL_GPU_VERTEXELEMENTFORMAT_SHORT4_NORM},
        {"ushort2_norm", SDL_GPU_VERTEXELEMENTFORMAT_USHORT2_NORM},
        {"ushort4_norm", SDL_GPU_VERTEXELEMENTFORMAT_USHORT4_NORM},
    };
    auto it = m.find(s);
    return it != m.end() ? it->second : SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
}

static SDL_GPUVertexInputRate toSDLVertexInputRate(const std::string &s) {
    static const std::unordered_map<std::string, SDL_GPUVertexInputRate> m = {
        {"vertex",   SDL_GPU_VERTEXINPUTRATE_VERTEX},
        {"instance", SDL_GPU_VERTEXINPUTRATE_INSTANCE},
    };
    auto it = m.find(s);
    return it != m.end() ? it->second : SDL_GPU_VERTEXINPUTRATE_VERTEX;
}

static SDL_GPUFillMode toSDLFillMode(const std::string &s) {
    static const std::unordered_map<std::string, SDL_GPUFillMode> m = {
        {"fill", SDL_GPU_FILLMODE_FILL},
        {"line", SDL_GPU_FILLMODE_LINE},
    };
    auto it = m.find(s);
    return it != m.end() ? it->second : SDL_GPU_FILLMODE_FILL;
}

static SDL_GPUCullMode toSDLCullMode(const std::string &s) {
    static const std::unordered_map<std::string, SDL_GPUCullMode> m = {
        {"none",  SDL_GPU_CULLMODE_NONE},
        {"front", SDL_GPU_CULLMODE_FRONT},
        {"back",  SDL_GPU_CULLMODE_BACK},
    };
    auto it = m.find(s);
    return it != m.end() ? it->second : SDL_GPU_CULLMODE_NONE;
}

static SDL_GPUFrontFace toSDLFrontFace(const std::string &s) {
    static const std::unordered_map<std::string, SDL_GPUFrontFace> m = {
        {"counter_clockwise", SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE},
        {"clockwise",         SDL_GPU_FRONTFACE_CLOCKWISE},
    };
    auto it = m.find(s);
    return it != m.end() ? it->second : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
}

static SDL_GPUCompareOp toSDLCompareOp(const std::string &s) {
    static const std::unordered_map<std::string, SDL_GPUCompareOp> m = {
        {"never",            SDL_GPU_COMPAREOP_NEVER},
        {"less",             SDL_GPU_COMPAREOP_LESS},
        {"equal",            SDL_GPU_COMPAREOP_EQUAL},
        {"less_or_equal",    SDL_GPU_COMPAREOP_LESS_OR_EQUAL},
        {"greater",          SDL_GPU_COMPAREOP_GREATER},
        {"not_equal",        SDL_GPU_COMPAREOP_NOT_EQUAL},
        {"greater_or_equal", SDL_GPU_COMPAREOP_GREATER_OR_EQUAL},
        {"always",           SDL_GPU_COMPAREOP_ALWAYS},
    };
    auto it = m.find(s);
    return it != m.end() ? it->second : SDL_GPU_COMPAREOP_LESS;
}

static SDL_GPUStencilOp toSDLStencilOp(const std::string &s) {
    static const std::unordered_map<std::string, SDL_GPUStencilOp> m = {
        {"keep",                SDL_GPU_STENCILOP_KEEP},
        {"zero",                SDL_GPU_STENCILOP_ZERO},
        {"replace",             SDL_GPU_STENCILOP_REPLACE},
        {"increment_and_clamp", SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP},
        {"decrement_and_clamp", SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP},
        {"invert",              SDL_GPU_STENCILOP_INVERT},
        {"increment_and_wrap",  SDL_GPU_STENCILOP_INCREMENT_AND_WRAP},
        {"decrement_and_wrap",  SDL_GPU_STENCILOP_DECREMENT_AND_WRAP},
    };
    auto it = m.find(s);
    return it != m.end() ? it->second : SDL_GPU_STENCILOP_KEEP;
}

static SDL_GPUBlendFactor toSDLBlendFactor(const std::string &s) {
    static const std::unordered_map<std::string, SDL_GPUBlendFactor> m = {
        {"zero",                SDL_GPU_BLENDFACTOR_ZERO},
        {"one",                 SDL_GPU_BLENDFACTOR_ONE},
        {"src_color",           SDL_GPU_BLENDFACTOR_SRC_COLOR},
        {"one_minus_src_color", SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR},
        {"dst_color",           SDL_GPU_BLENDFACTOR_DST_COLOR},
        {"one_minus_dst_color", SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR},
        {"src_alpha",           SDL_GPU_BLENDFACTOR_SRC_ALPHA},
        {"one_minus_src_alpha", SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA},
        {"dst_alpha",           SDL_GPU_BLENDFACTOR_DST_ALPHA},
        {"one_minus_dst_alpha", SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA},
    };
    auto it = m.find(s);
    return it != m.end() ? it->second : SDL_GPU_BLENDFACTOR_ZERO;
}

static SDL_GPUBlendOp toSDLBlendOp(const std::string &s) {
    static const std::unordered_map<std::string, SDL_GPUBlendOp> m = {
        {"add",              SDL_GPU_BLENDOP_ADD},
        {"subtract",         SDL_GPU_BLENDOP_SUBTRACT},
        {"reverse_subtract", SDL_GPU_BLENDOP_REVERSE_SUBTRACT},
        {"min",              SDL_GPU_BLENDOP_MIN},
        {"max",              SDL_GPU_BLENDOP_MAX},
    };
    auto it = m.find(s);
    return it != m.end() ? it->second : SDL_GPU_BLENDOP_ADD;
}

static SDL_GPUTextureFormat toSDLTextureFormat(const std::string &s) {
    static const std::unordered_map<std::string, SDL_GPUTextureFormat> m = {
        {"a8_unorm",                SDL_GPU_TEXTUREFORMAT_A8_UNORM},
        {"r8_unorm",                SDL_GPU_TEXTUREFORMAT_R8_UNORM},
        {"r8g8_unorm",              SDL_GPU_TEXTUREFORMAT_R8G8_UNORM},
        {"r8g8b8a8_unorm",          SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM},
        {"b8g8r8a8_unorm",          SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM},
        {"r16_unorm",               SDL_GPU_TEXTUREFORMAT_R16_UNORM},
        {"r16g16_unorm",            SDL_GPU_TEXTUREFORMAT_R16G16_UNORM},
        {"r16g16b16a16_unorm",      SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM},
        {"r10g10b10a2_unorm",       SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM},
        {"b5g6r5_unorm",            SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM},
        {"b5g5r5a1_unorm",          SDL_GPU_TEXTUREFORMAT_B5G5R5A1_UNORM},
        {"b4g4r4a4_unorm",          SDL_GPU_TEXTUREFORMAT_B4G4R4A4_UNORM},
        {"r8g8b8a8_unorm_srgb",     SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB},
        {"b8g8r8a8_unorm_srgb",     SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB},
        {"bc1_rgba_unorm",          SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM},
        {"bc2_rgba_unorm",          SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM},
        {"bc3_rgba_unorm",          SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM},
        {"bc4_r_unorm",             SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM},
        {"bc5_rg_unorm",            SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM},
        {"bc7_rgba_unorm",          SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM},
        {"bc6h_rgb_float",          SDL_GPU_TEXTUREFORMAT_BC6H_RGB_FLOAT},
        {"bc6h_rgb_ufloat",         SDL_GPU_TEXTUREFORMAT_BC6H_RGB_UFLOAT},
        {"r8_snorm",                SDL_GPU_TEXTUREFORMAT_R8_SNORM},
        {"r8g8_snorm",              SDL_GPU_TEXTUREFORMAT_R8G8_SNORM},
        {"r8g8b8a8_snorm",          SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM},
        {"r16_snorm",               SDL_GPU_TEXTUREFORMAT_R16_SNORM},
        {"r16g16_snorm",            SDL_GPU_TEXTUREFORMAT_R16G16_SNORM},
        {"r16g16b16a16_snorm",      SDL_GPU_TEXTUREFORMAT_R16G16B16A16_SNORM},
        {"r16g16b16a16_float",      SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT},
        {"r32_float",               SDL_GPU_TEXTUREFORMAT_R32_FLOAT},
        {"r32g32_float",            SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT},
        {"r32g32b32a32_float",      SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT},
        {"r11g11b10_ufloat",        SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT},
        {"r8g8b8a8_uint",           SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT},
        {"r16g16b16a16_uint",       SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT},
        {"d16_unorm",               SDL_GPU_TEXTUREFORMAT_D16_UNORM},
        {"d24_unorm",               SDL_GPU_TEXTUREFORMAT_D24_UNORM},
        {"d32_float",               SDL_GPU_TEXTUREFORMAT_D32_FLOAT},
        {"d24_unorm_s8_uint",       SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT},
        {"d32_float_s8_uint",       SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT},
    };
    auto it = m.find(s);
    return it != m.end() ? it->second : SDL_GPU_TEXTUREFORMAT_INVALID;
}

static SDL_GPUSampleCount toSDLSampleCount(int n) {
    switch (n) {
        case 2: return SDL_GPU_SAMPLECOUNT_2;
        case 4: return SDL_GPU_SAMPLECOUNT_4;
        case 8: return SDL_GPU_SAMPLECOUNT_8;
        default: return SDL_GPU_SAMPLECOUNT_1;
    }
}

// =====================================================================
// Pipeline construction
// =====================================================================

Pipeline::Pipeline(const core::NamespacedId &id,
                   std::filesystem::path filePath,
                   core::ResourceMode mode,
                   SDL_GPUGraphicsPipeline *pipeline)
    : core::Resource(id, std::move(filePath), mode),
      _pipeline(pipeline) {}

Pipeline::~Pipeline() {
    if (_pipeline) {
        auto *device = runtime::Application::instance()
                           .renderer().gpuDevice();
        if (device) {
            SDL_ReleaseGPUGraphicsPipeline(device, _pipeline);
        }
        _pipeline = nullptr;
    }
}

Pipeline::Pipeline(Pipeline &&other) noexcept
    : core::Resource(std::move(other)),
      _pipeline(other._pipeline) {
    other._pipeline = nullptr;
}

Pipeline &Pipeline::operator=(Pipeline &&other) noexcept {
    if (this != &other) {
        if (_pipeline) {
            auto *device = runtime::Application::instance()
                               .renderer().gpuDevice();
            if (device) {
                SDL_ReleaseGPUGraphicsPipeline(device, _pipeline);
            }
        }
        core::Resource::operator=(std::move(other));
        _pipeline = other._pipeline;
        other._pipeline = nullptr;
    }
    return *this;
}

Pipeline::Handle Pipeline::resolve(const core::NamespacedId &id,
                                    std::vector<uint8_t> data,
                                    std::filesystem::path filePath,
                                    core::ResourceMode mode,
                                    SDL_GPUTextureFormat format) {
    auto *device = runtime::Application::instance()
                       .renderer().gpuDevice();
    if (!device || data.empty()) return {};

    // Parse pipeline JSON
    std::string jsonStr(data.begin(), data.end());
    auto pipelineVal = core::Value::parse(jsonStr);
    if (!pipelineVal.isObject()) {
        core::Logger::instance().error(
            "Pipeline: Invalid JSON: {}", id.toString());
        return {};
    }
    const auto &v = pipelineVal;

    // --- Parse JSON fields ---

    // Shaders
    auto vsStr = v["vertex_shader"].asString();
    if (vsStr.empty()) {
        core::Logger::instance().error("Pipeline: Missing vertex_shader");
        return {};
    }
    auto vertexShaderId = core::NamespacedId::parse(vsStr);

    std::optional<core::NamespacedId> fragmentShaderId;
    auto fsStr = v["fragment_shader"].asString();
    if (!fsStr.empty()) {
        fragmentShaderId = core::NamespacedId::parse(fsStr);
    }

    // Primitive type
    auto primitiveType = toSDLPrimitiveType(v["primitive_type"].asString("triangle_list"));

    // Vertex input
    std::vector<SDL_GPUVertexBufferDescription> vertexBuffers;
    std::vector<SDL_GPUVertexAttribute> vertexAttributes;
    auto vi = v["vertex_input"];
    if (vi.isObject()) {
        auto bufs = vi["buffers"];
        if (bufs.isArray()) {
            for (size_t i = 0; i < bufs.size(); ++i) {
                const auto &b = bufs[i];
                SDL_GPUVertexBufferDescription d{};
                d.slot = static_cast<Uint32>(b["slot"].asInt(0));
                d.pitch = static_cast<Uint32>(b["stride"].asInt(0));
                d.input_rate = toSDLVertexInputRate(b["input_rate"].asString("vertex"));
                d.instance_step_rate = 0;
                vertexBuffers.push_back(d);
            }
        }
        auto attrs = vi["attributes"];
        if (attrs.isArray()) {
            for (size_t i = 0; i < attrs.size(); ++i) {
                const auto &a = attrs[i];
                SDL_GPUVertexAttribute d{};
                d.location = static_cast<Uint32>(a["location"].asInt(0));
                d.buffer_slot = static_cast<Uint32>(a["buffer_slot"].asInt(0));
                d.format = toSDLVertexFormat(a["format"].asString("float4"));
                d.offset = static_cast<Uint32>(a["offset"].asInt(0));
                vertexAttributes.push_back(d);
            }
        }
    }

    // Rasterizer
    SDL_GPURasterizerState rasterizer{};
    auto rast = v["rasterizer"];
    if (rast.isObject()) {
        rasterizer.fill_mode = toSDLFillMode(rast["fill_mode"].asString("fill"));
        rasterizer.cull_mode = toSDLCullMode(rast["cull_mode"].asString("none"));
        rasterizer.front_face = toSDLFrontFace(rast["front_face"].asString("counter_clockwise"));
        rasterizer.depth_bias_constant_factor = static_cast<float>(rast["depth_bias_constant_factor"].asDouble(0.0));
        rasterizer.depth_bias_clamp = static_cast<float>(rast["depth_bias_clamp"].asDouble(0.0));
        rasterizer.depth_bias_slope_factor = static_cast<float>(rast["depth_bias_slope_factor"].asDouble(0.0));
        rasterizer.enable_depth_bias = rast["enable_depth_bias"].asBool(false);
        rasterizer.enable_depth_clip = rast["enable_depth_clip"].asBool(true);
    }

    // Depth stencil (optional)
    SDL_GPUDepthStencilState depthStencil{};
    bool hasDepthStencil = false;
    auto ds = v["depth_stencil"];
    if (ds.isObject()) {
        hasDepthStencil = true;
        depthStencil.compare_op = toSDLCompareOp(ds["compare_op"].asString("less"));
        depthStencil.enable_depth_test = ds["depth_test"].asBool(true);
        depthStencil.enable_depth_write = ds["depth_write"].asBool(true);
        depthStencil.enable_stencil_test = ds["enable_stencil_test"].asBool(false);
        depthStencil.compare_mask = static_cast<Uint8>(ds["compare_mask"].asInt(0xFF));
        depthStencil.write_mask = static_cast<Uint8>(ds["write_mask"].asInt(0xFF));

        auto bs = ds["back_stencil"];
        if (bs.isObject()) {
            depthStencil.back_stencil_state.fail_op = toSDLStencilOp(bs["fail_op"].asString("keep"));
            depthStencil.back_stencil_state.pass_op = toSDLStencilOp(bs["pass_op"].asString("keep"));
            depthStencil.back_stencil_state.depth_fail_op = toSDLStencilOp(bs["depth_fail_op"].asString("keep"));
            depthStencil.back_stencil_state.compare_op = toSDLCompareOp(bs["compare_op"].asString("always"));
        }
        auto fs = ds["front_stencil"];
        if (fs.isObject()) {
            depthStencil.front_stencil_state.fail_op = toSDLStencilOp(fs["fail_op"].asString("keep"));
            depthStencil.front_stencil_state.pass_op = toSDLStencilOp(fs["pass_op"].asString("keep"));
            depthStencil.front_stencil_state.depth_fail_op = toSDLStencilOp(fs["depth_fail_op"].asString("keep"));
            depthStencil.front_stencil_state.compare_op = toSDLCompareOp(fs["compare_op"].asString("always"));
        }
    }

    // Color targets
    std::vector<SDL_GPUColorTargetDescription> colorTargets;
    auto cts = v["color_targets"];
    if (cts.isArray()) {
        for (size_t i = 0; i < cts.size(); ++i) {
            const auto &ct = cts[i];
            SDL_GPUColorTargetDescription d{};
            auto fmtStr = ct["format"].asString("swapchain");
            if (fmtStr == "swapchain") {
                d.format = format;
            } else {
                d.format = toSDLTextureFormat(fmtStr);
            }
            auto blend = ct["blend"];
            if (blend.isObject()) {
                d.blend_state.src_color_blendfactor = toSDLBlendFactor(blend["src_color"].asString("src_alpha"));
                d.blend_state.dst_color_blendfactor = toSDLBlendFactor(blend["dst_color"].asString("one_minus_src_alpha"));
                d.blend_state.color_blend_op = toSDLBlendOp(blend["color_op"].asString("add"));
                d.blend_state.src_alpha_blendfactor = toSDLBlendFactor(blend["src_alpha"].asString("one"));
                d.blend_state.dst_alpha_blendfactor = toSDLBlendFactor(blend["dst_alpha"].asString("one_minus_src_alpha"));
                d.blend_state.alpha_blend_op = toSDLBlendOp(blend["alpha_op"].asString("add"));
                d.blend_state.color_write_mask = static_cast<SDL_GPUColorComponentFlags>(blend["color_write_mask"].asInt(0xF));
                d.blend_state.enable_blend = blend["enable_blend"].asBool(true);
                d.blend_state.enable_color_write_mask = blend["enable_color_write_mask"].asBool(false);
            }
            colorTargets.push_back(d);
        }
    }

    // Multisample
    SDL_GPUMultisampleState multisample{};
    auto ms = v["multisample"];
    if (ms.isObject()) {
        multisample.sample_count = toSDLSampleCount(ms["sample_count"].asInt(1));
        multisample.sample_mask = static_cast<Uint32>(ms["sample_mask"].asInt(0));
        multisample.enable_alpha_to_coverage = ms["enable_alpha_to_coverage"].asBool(false);
    }

    // --- Load shaders ---

    auto &assetMgr = runtime::Application::instance().assetManager();
    std::map<core::NamespacedId, SDL_GPUShader *> shaderMap;

    auto *vs = Shader::loadFromAsset(assetMgr, vertexShaderId,
                                      SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    if (!vs) return {};
    shaderMap[vertexShaderId] = vs;

    if (fragmentShaderId.has_value()) {
        auto *fs = Shader::loadFromAsset(assetMgr, *fragmentShaderId,
                                          SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
        if (!fs) {
            Shader::slotMap().clear();
            return {};
        }
        shaderMap[*fragmentShaderId] = fs;
    }

    // --- Create GPU pipeline ---

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vs;
    info.fragment_shader = fragmentShaderId.has_value()
        ? shaderMap[*fragmentShaderId] : nullptr;
    info.primitive_type = primitiveType;

    info.vertex_input_state.vertex_buffer_descriptions = vertexBuffers.data();
    info.vertex_input_state.num_vertex_buffers = static_cast<Uint32>(vertexBuffers.size());
    info.vertex_input_state.vertex_attributes = vertexAttributes.data();
    info.vertex_input_state.num_vertex_attributes = static_cast<Uint32>(vertexAttributes.size());

    info.rasterizer_state = rasterizer;

    if (hasDepthStencil) {
        info.depth_stencil_state = depthStencil;
        info.target_info.has_depth_stencil_target = true;
        info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    }

    info.target_info.num_color_targets = static_cast<Uint32>(colorTargets.size());
    info.target_info.color_target_descriptions = colorTargets.data();

    info.multisample_state = multisample;

    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);

    // Release shaders — pipeline owns a copy internally
    Shader::slotMap().clear();

    if (!pipeline) {
        core::Logger::instance().error(
            "Pipeline: Failed to create GPU pipeline: {}", SDL_GetError());
        return {};
    }

    Pipeline p(id, std::move(filePath), mode, pipeline);
    return Handle(slotMap().insert(std::move(p)));
}

} // namespace noix::video
