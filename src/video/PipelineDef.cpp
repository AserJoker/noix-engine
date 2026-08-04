#include "video/PipelineDef.h"
#include "core/Logger.h"

#include <unordered_map>

namespace noix::video {

// =====================================================================
// SDL enum ↔ string maps
// =====================================================================

// ---- Primitive type ----

static const std::unordered_map<std::string, SDL_GPUPrimitiveType> kStrToPrimitiveType = {
    {"triangle_list",  SDL_GPU_PRIMITIVETYPE_TRIANGLELIST},
    {"triangle_strip", SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP},
    {"line_list",      SDL_GPU_PRIMITIVETYPE_LINELIST},
    {"line_strip",     SDL_GPU_PRIMITIVETYPE_LINESTRIP},
    {"point_list",     SDL_GPU_PRIMITIVETYPE_POINTLIST},
};

static const std::unordered_map<SDL_GPUPrimitiveType, std::string> kPrimitiveTypeToStr = {
    {SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, "triangle_list"},
    {SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP, "triangle_strip"},
    {SDL_GPU_PRIMITIVETYPE_LINELIST,      "line_list"},
    {SDL_GPU_PRIMITIVETYPE_LINESTRIP,     "line_strip"},
    {SDL_GPU_PRIMITIVETYPE_POINTLIST,     "point_list"},
};

SDL_GPUPrimitiveType toSDLPrimitiveType(const std::string &s) {
    auto it = kStrToPrimitiveType.find(s);
    return it != kStrToPrimitiveType.end() ? it->second : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
}

std::string fromSDLPrimitiveType(SDL_GPUPrimitiveType t) {
    auto it = kPrimitiveTypeToStr.find(t);
    return it != kPrimitiveTypeToStr.end() ? it->second : "triangle_list";
}

// ---- Vertex format ----

static const std::unordered_map<std::string, SDL_GPUVertexElementFormat> kStrToVertexFormat = {
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

static const std::unordered_map<SDL_GPUVertexElementFormat, std::string> kVertexFormatToStr = {
    {SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,      "float2"},
    {SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,      "float3"},
    {SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,      "float4"},
    {SDL_GPU_VERTEXELEMENTFORMAT_BYTE2_NORM,  "byte2_norm"},
    {SDL_GPU_VERTEXELEMENTFORMAT_BYTE4_NORM,  "byte4_norm"},
    {SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2_NORM, "ubyte2_norm"},
    {SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, "ubyte4_norm"},
    {SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM, "short2_norm"},
    {SDL_GPU_VERTEXELEMENTFORMAT_SHORT4_NORM, "short4_norm"},
    {SDL_GPU_VERTEXELEMENTFORMAT_USHORT2_NORM, "ushort2_norm"},
    {SDL_GPU_VERTEXELEMENTFORMAT_USHORT4_NORM, "ushort4_norm"},
};

SDL_GPUVertexElementFormat toSDLVertexFormat(const std::string &s) {
    auto it = kStrToVertexFormat.find(s);
    return it != kStrToVertexFormat.end() ? it->second : SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
}

std::string fromSDLVertexFormat(SDL_GPUVertexElementFormat f) {
    auto it = kVertexFormatToStr.find(f);
    return it != kVertexFormatToStr.end() ? it->second : "float4";
}

// ---- Vertex input rate ----

static const std::unordered_map<std::string, SDL_GPUVertexInputRate> kStrToVertexInputRate = {
    {"vertex",   SDL_GPU_VERTEXINPUTRATE_VERTEX},
    {"instance", SDL_GPU_VERTEXINPUTRATE_INSTANCE},
};

static const std::unordered_map<SDL_GPUVertexInputRate, std::string> kVertexInputRateToStr = {
    {SDL_GPU_VERTEXINPUTRATE_VERTEX,   "vertex"},
    {SDL_GPU_VERTEXINPUTRATE_INSTANCE, "instance"},
};

SDL_GPUVertexInputRate toSDLVertexInputRate(const std::string &s) {
    auto it = kStrToVertexInputRate.find(s);
    return it != kStrToVertexInputRate.end() ? it->second : SDL_GPU_VERTEXINPUTRATE_VERTEX;
}

std::string fromSDLVertexInputRate(SDL_GPUVertexInputRate r) {
    auto it = kVertexInputRateToStr.find(r);
    return it != kVertexInputRateToStr.end() ? it->second : "vertex";
}

// ---- Fill mode ----

static const std::unordered_map<std::string, SDL_GPUFillMode> kStrToFillMode = {
    {"fill", SDL_GPU_FILLMODE_FILL},
    {"line", SDL_GPU_FILLMODE_LINE},
};

static const std::unordered_map<SDL_GPUFillMode, std::string> kFillModeToStr = {
    {SDL_GPU_FILLMODE_FILL, "fill"},
    {SDL_GPU_FILLMODE_LINE, "line"},
};

SDL_GPUFillMode toSDLFillMode(const std::string &s) {
    auto it = kStrToFillMode.find(s);
    return it != kStrToFillMode.end() ? it->second : SDL_GPU_FILLMODE_FILL;
}

std::string fromSDLFillMode(SDL_GPUFillMode m) {
    auto it = kFillModeToStr.find(m);
    return it != kFillModeToStr.end() ? it->second : "fill";
}

// ---- Cull mode ----

static const std::unordered_map<std::string, SDL_GPUCullMode> kStrToCullMode = {
    {"none",  SDL_GPU_CULLMODE_NONE},
    {"front", SDL_GPU_CULLMODE_FRONT},
    {"back",  SDL_GPU_CULLMODE_BACK},
};

static const std::unordered_map<SDL_GPUCullMode, std::string> kCullModeToStr = {
    {SDL_GPU_CULLMODE_NONE,  "none"},
    {SDL_GPU_CULLMODE_FRONT, "front"},
    {SDL_GPU_CULLMODE_BACK,  "back"},
};

SDL_GPUCullMode toSDLCullMode(const std::string &s) {
    auto it = kStrToCullMode.find(s);
    return it != kStrToCullMode.end() ? it->second : SDL_GPU_CULLMODE_NONE;
}

std::string fromSDLCullMode(SDL_GPUCullMode m) {
    auto it = kCullModeToStr.find(m);
    return it != kCullModeToStr.end() ? it->second : "none";
}

// ---- Front face ----

static const std::unordered_map<std::string, SDL_GPUFrontFace> kStrToFrontFace = {
    {"counter_clockwise", SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE},
    {"clockwise",         SDL_GPU_FRONTFACE_CLOCKWISE},
};

static const std::unordered_map<SDL_GPUFrontFace, std::string> kFrontFaceToStr = {
    {SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE, "counter_clockwise"},
    {SDL_GPU_FRONTFACE_CLOCKWISE,         "clockwise"},
};

SDL_GPUFrontFace toSDLFrontFace(const std::string &s) {
    auto it = kStrToFrontFace.find(s);
    return it != kStrToFrontFace.end() ? it->second : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
}

std::string fromSDLFrontFace(SDL_GPUFrontFace f) {
    auto it = kFrontFaceToStr.find(f);
    return it != kFrontFaceToStr.end() ? it->second : "counter_clockwise";
}

// ---- Compare op ----

static const std::unordered_map<std::string, SDL_GPUCompareOp> kStrToCompareOp = {
    {"never",            SDL_GPU_COMPAREOP_NEVER},
    {"less",             SDL_GPU_COMPAREOP_LESS},
    {"equal",            SDL_GPU_COMPAREOP_EQUAL},
    {"less_or_equal",    SDL_GPU_COMPAREOP_LESS_OR_EQUAL},
    {"greater",          SDL_GPU_COMPAREOP_GREATER},
    {"not_equal",        SDL_GPU_COMPAREOP_NOT_EQUAL},
    {"greater_or_equal", SDL_GPU_COMPAREOP_GREATER_OR_EQUAL},
    {"always",           SDL_GPU_COMPAREOP_ALWAYS},
};

static const std::unordered_map<SDL_GPUCompareOp, std::string> kCompareOpToStr = {
    {SDL_GPU_COMPAREOP_NEVER,          "never"},
    {SDL_GPU_COMPAREOP_LESS,           "less"},
    {SDL_GPU_COMPAREOP_EQUAL,          "equal"},
    {SDL_GPU_COMPAREOP_LESS_OR_EQUAL,  "less_or_equal"},
    {SDL_GPU_COMPAREOP_GREATER,        "greater"},
    {SDL_GPU_COMPAREOP_NOT_EQUAL,      "not_equal"},
    {SDL_GPU_COMPAREOP_GREATER_OR_EQUAL, "greater_or_equal"},
    {SDL_GPU_COMPAREOP_ALWAYS,         "always"},
};

SDL_GPUCompareOp toSDLCompareOp(const std::string &s) {
    auto it = kStrToCompareOp.find(s);
    return it != kStrToCompareOp.end() ? it->second : SDL_GPU_COMPAREOP_LESS;
}

std::string fromSDLCompareOp(SDL_GPUCompareOp op) {
    auto it = kCompareOpToStr.find(op);
    return it != kCompareOpToStr.end() ? it->second : "less";
}

// ---- Stencil op ----

static const std::unordered_map<std::string, SDL_GPUStencilOp> kStrToStencilOp = {
    {"keep",               SDL_GPU_STENCILOP_KEEP},
    {"zero",               SDL_GPU_STENCILOP_ZERO},
    {"replace",            SDL_GPU_STENCILOP_REPLACE},
    {"increment_and_clamp", SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP},
    {"decrement_and_clamp", SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP},
    {"invert",             SDL_GPU_STENCILOP_INVERT},
    {"increment_and_wrap", SDL_GPU_STENCILOP_INCREMENT_AND_WRAP},
    {"decrement_and_wrap", SDL_GPU_STENCILOP_DECREMENT_AND_WRAP},
};

static const std::unordered_map<SDL_GPUStencilOp, std::string> kStencilOpToStr = {
    {SDL_GPU_STENCILOP_KEEP,                "keep"},
    {SDL_GPU_STENCILOP_ZERO,                "zero"},
    {SDL_GPU_STENCILOP_REPLACE,             "replace"},
    {SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP, "increment_and_clamp"},
    {SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP, "decrement_and_clamp"},
    {SDL_GPU_STENCILOP_INVERT,              "invert"},
    {SDL_GPU_STENCILOP_INCREMENT_AND_WRAP,  "increment_and_wrap"},
    {SDL_GPU_STENCILOP_DECREMENT_AND_WRAP,  "decrement_and_wrap"},
};

SDL_GPUStencilOp toSDLStencilOp(const std::string &s) {
    auto it = kStrToStencilOp.find(s);
    return it != kStrToStencilOp.end() ? it->second : SDL_GPU_STENCILOP_KEEP;
}

std::string fromSDLStencilOp(SDL_GPUStencilOp op) {
    auto it = kStencilOpToStr.find(op);
    return it != kStencilOpToStr.end() ? it->second : "keep";
}

// ---- Blend factor ----

static const std::unordered_map<std::string, SDL_GPUBlendFactor> kStrToBlendFactor = {
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

static const std::unordered_map<SDL_GPUBlendFactor, std::string> kBlendFactorToStr = {
    {SDL_GPU_BLENDFACTOR_ZERO,               "zero"},
    {SDL_GPU_BLENDFACTOR_ONE,                "one"},
    {SDL_GPU_BLENDFACTOR_SRC_COLOR,          "src_color"},
    {SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR, "one_minus_src_color"},
    {SDL_GPU_BLENDFACTOR_DST_COLOR,          "dst_color"},
    {SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR, "one_minus_dst_color"},
    {SDL_GPU_BLENDFACTOR_SRC_ALPHA,          "src_alpha"},
    {SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, "one_minus_src_alpha"},
    {SDL_GPU_BLENDFACTOR_DST_ALPHA,          "dst_alpha"},
    {SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA, "one_minus_dst_alpha"},
};

SDL_GPUBlendFactor toSDLBlendFactor(const std::string &s) {
    auto it = kStrToBlendFactor.find(s);
    return it != kStrToBlendFactor.end() ? it->second : SDL_GPU_BLENDFACTOR_ZERO;
}

std::string fromSDLBlendFactor(SDL_GPUBlendFactor f) {
    auto it = kBlendFactorToStr.find(f);
    return it != kBlendFactorToStr.end() ? it->second : "zero";
}

// ---- Blend op ----

static const std::unordered_map<std::string, SDL_GPUBlendOp> kStrToBlendOp = {
    {"add",              SDL_GPU_BLENDOP_ADD},
    {"subtract",         SDL_GPU_BLENDOP_SUBTRACT},
    {"reverse_subtract", SDL_GPU_BLENDOP_REVERSE_SUBTRACT},
    {"min",              SDL_GPU_BLENDOP_MIN},
    {"max",              SDL_GPU_BLENDOP_MAX},
};

static const std::unordered_map<SDL_GPUBlendOp, std::string> kBlendOpToStr = {
    {SDL_GPU_BLENDOP_ADD,             "add"},
    {SDL_GPU_BLENDOP_SUBTRACT,        "subtract"},
    {SDL_GPU_BLENDOP_REVERSE_SUBTRACT, "reverse_subtract"},
    {SDL_GPU_BLENDOP_MIN,             "min"},
    {SDL_GPU_BLENDOP_MAX,             "max"},
};

SDL_GPUBlendOp toSDLBlendOp(const std::string &s) {
    auto it = kStrToBlendOp.find(s);
    return it != kStrToBlendOp.end() ? it->second : SDL_GPU_BLENDOP_ADD;
}

std::string fromSDLBlendOp(SDL_GPUBlendOp op) {
    auto it = kBlendOpToStr.find(op);
    return it != kBlendOpToStr.end() ? it->second : "add";
}

// ---- Texture format ----

static const std::unordered_map<std::string, SDL_GPUTextureFormat> kStrToTextureFormat = {
    {"a8_unorm",                SDL_GPU_TEXTUREFORMAT_A8_UNORM},
    {"r8_unorm",                SDL_GPU_TEXTUREFORMAT_R8_UNORM},
    {"r8g8_unorm",              SDL_GPU_TEXTUREFORMAT_R8G8_UNORM},
    {"r8g8b8a8_unorm",          SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM},
    {"r16_unorm",               SDL_GPU_TEXTUREFORMAT_R16_UNORM},
    {"r16g16_unorm",            SDL_GPU_TEXTUREFORMAT_R16G16_UNORM},
    {"r16g16b16a16_unorm",      SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM},
    {"r10g10b10a2_unorm",       SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM},
    {"b5g6r5_unorm",            SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM},
    {"b5g5r5a1_unorm",          SDL_GPU_TEXTUREFORMAT_B5G5R5A1_UNORM},
    {"b4g4r4a4_unorm",          SDL_GPU_TEXTUREFORMAT_B4G4R4A4_UNORM},
    {"b8g8r8a8_unorm",          SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM},
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

static const std::unordered_map<SDL_GPUTextureFormat, std::string> kTextureFormatToStr = {
    {SDL_GPU_TEXTUREFORMAT_A8_UNORM,                "a8_unorm"},
    {SDL_GPU_TEXTUREFORMAT_R8_UNORM,                "r8_unorm"},
    {SDL_GPU_TEXTUREFORMAT_R8G8_UNORM,              "r8g8_unorm"},
    {SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,          "r8g8b8a8_unorm"},
    {SDL_GPU_TEXTUREFORMAT_R16_UNORM,               "r16_unorm"},
    {SDL_GPU_TEXTUREFORMAT_R16G16_UNORM,            "r16g16_unorm"},
    {SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM,      "r16g16b16a16_unorm"},
    {SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM,       "r10g10b10a2_unorm"},
    {SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM,            "b5g6r5_unorm"},
    {SDL_GPU_TEXTUREFORMAT_B5G5R5A1_UNORM,          "b5g5r5a1_unorm"},
    {SDL_GPU_TEXTUREFORMAT_B4G4R4A4_UNORM,          "b4g4r4a4_unorm"},
    {SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,          "b8g8r8a8_unorm"},
    {SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,     "r8g8b8a8_unorm_srgb"},
    {SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB,     "b8g8r8a8_unorm_srgb"},
    {SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM,          "bc1_rgba_unorm"},
    {SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM,          "bc2_rgba_unorm"},
    {SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM,          "bc3_rgba_unorm"},
    {SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM,             "bc4_r_unorm"},
    {SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM,            "bc5_rg_unorm"},
    {SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM,          "bc7_rgba_unorm"},
    {SDL_GPU_TEXTUREFORMAT_BC6H_RGB_FLOAT,          "bc6h_rgb_float"},
    {SDL_GPU_TEXTUREFORMAT_BC6H_RGB_UFLOAT,         "bc6h_rgb_ufloat"},
    {SDL_GPU_TEXTUREFORMAT_R8_SNORM,                "r8_snorm"},
    {SDL_GPU_TEXTUREFORMAT_R8G8_SNORM,              "r8g8_snorm"},
    {SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM,          "r8g8b8a8_snorm"},
    {SDL_GPU_TEXTUREFORMAT_R16_SNORM,               "r16_snorm"},
    {SDL_GPU_TEXTUREFORMAT_R16G16_SNORM,            "r16g16_snorm"},
    {SDL_GPU_TEXTUREFORMAT_R16G16B16A16_SNORM,      "r16g16b16a16_snorm"},
    {SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,      "r16g16b16a16_float"},
    {SDL_GPU_TEXTUREFORMAT_R32_FLOAT,               "r32_float"},
    {SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT,            "r32g32_float"},
    {SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,      "r32g32b32a32_float"},
    {SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT,        "r11g11b10_ufloat"},
    {SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT,           "r8g8b8a8_uint"},
    {SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT,       "r16g16b16a16_uint"},
    {SDL_GPU_TEXTUREFORMAT_D16_UNORM,               "d16_unorm"},
    {SDL_GPU_TEXTUREFORMAT_D24_UNORM,               "d24_unorm"},
    {SDL_GPU_TEXTUREFORMAT_D32_FLOAT,               "d32_float"},
    {SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,       "d24_unorm_s8_uint"},
    {SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,       "d32_float_s8_uint"},
};

SDL_GPUTextureFormat toSDLTextureFormat(const std::string &s) {
    auto it = kStrToTextureFormat.find(s);
    return it != kStrToTextureFormat.end() ? it->second : SDL_GPU_TEXTUREFORMAT_INVALID;
}

std::string fromSDLTextureFormat(SDL_GPUTextureFormat f) {
    auto it = kTextureFormatToStr.find(f);
    return it != kTextureFormatToStr.end() ? it->second : "invalid";
}

SDL_GPUSampleCount toSDLSampleCount(int n) {
    switch (n) {
        case 2: return SDL_GPU_SAMPLECOUNT_2;
        case 4: return SDL_GPU_SAMPLECOUNT_4;
        case 8: return SDL_GPU_SAMPLECOUNT_8;
        default: return SDL_GPU_SAMPLECOUNT_1;
    }
}

int fromSDLSampleCount(SDL_GPUSampleCount sc) {
    switch (sc) {
        case SDL_GPU_SAMPLECOUNT_2: return 2;
        case SDL_GPU_SAMPLECOUNT_4: return 4;
        case SDL_GPU_SAMPLECOUNT_8: return 8;
        default: return 1;
    }
}

// =====================================================================
// PipelineDef::parse
// =====================================================================

std::optional<PipelineDef> PipelineDef::parse(const core::Value &v) {
    if (!v.isObject()) return std::nullopt;

    PipelineDef def;

    // Shaders
    auto vsStr = v["vertex_shader"].asString();
    if (vsStr.empty()) return std::nullopt;
    def._vertexShader = core::NamespacedId::parse(vsStr);

    auto fsStr = v["fragment_shader"].asString();
    if (!fsStr.empty()) {
        def._fragmentShader = core::NamespacedId::parse(fsStr);
    }

    // Primitive type
    def._primitiveType = toSDLPrimitiveType(v["primitive_type"].asString("triangle_list"));

    // Vertex input
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
                def._vertexBuffers.push_back(d);
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
                def._vertexAttributes.push_back(d);
            }
        }
    }

    // Rasterizer
    auto rast = v["rasterizer"];
    if (rast.isObject()) {
        def._rasterizer.fillMode = toSDLFillMode(rast["fill_mode"].asString("fill"));
        def._rasterizer.cullMode = toSDLCullMode(rast["cull_mode"].asString("none"));
        def._rasterizer.frontFace = toSDLFrontFace(rast["front_face"].asString("counter_clockwise"));
        def._rasterizer.depthBiasConstantFactor = static_cast<float>(rast["depth_bias_constant_factor"].asDouble(0.0));
        def._rasterizer.depthBiasClamp = static_cast<float>(rast["depth_bias_clamp"].asDouble(0.0));
        def._rasterizer.depthBiasSlopeFactor = static_cast<float>(rast["depth_bias_slope_factor"].asDouble(0.0));
        def._rasterizer.enableDepthBias = rast["enable_depth_bias"].asBool(false);
        def._rasterizer.enableDepthClip = rast["enable_depth_clip"].asBool(true);
    }

    // Depth stencil (optional)
    auto ds = v["depth_stencil"];
    if (ds.isObject()) {
        def._depthStencil = DepthStencilDef{};
        def._depthStencil->compareOp = toSDLCompareOp(ds["compare_op"].asString("less"));
        def._depthStencil->enableDepthTest = ds["depth_test"].asBool(true);
        def._depthStencil->enableDepthWrite = ds["depth_write"].asBool(true);
        def._depthStencil->enableStencilTest = ds["enable_stencil_test"].asBool(false);
        def._depthStencil->compareMask = static_cast<Uint8>(ds["compare_mask"].asInt(0xFF));
        def._depthStencil->writeMask = static_cast<Uint8>(ds["write_mask"].asInt(0xFF));

        // Back stencil
        auto bs = ds["back_stencil"];
        if (bs.isObject()) {
            def._depthStencil->backStencil.failOp = toSDLStencilOp(bs["fail_op"].asString("keep"));
            def._depthStencil->backStencil.passOp = toSDLStencilOp(bs["pass_op"].asString("keep"));
            def._depthStencil->backStencil.depthFailOp = toSDLStencilOp(bs["depth_fail_op"].asString("keep"));
            def._depthStencil->backStencil.compareOp = toSDLCompareOp(bs["compare_op"].asString("always"));
        }
        // Front stencil
        auto fs = ds["front_stencil"];
        if (fs.isObject()) {
            def._depthStencil->frontStencil.failOp = toSDLStencilOp(fs["fail_op"].asString("keep"));
            def._depthStencil->frontStencil.passOp = toSDLStencilOp(fs["pass_op"].asString("keep"));
            def._depthStencil->frontStencil.depthFailOp = toSDLStencilOp(fs["depth_fail_op"].asString("keep"));
            def._depthStencil->frontStencil.compareOp = toSDLCompareOp(fs["compare_op"].asString("always"));
        }
    }

    // Color targets
    auto cts = v["color_targets"];
    if (cts.isArray()) {
        for (size_t i = 0; i < cts.size(); ++i) {
            const auto &ct = cts[i];
            ColorTargetDef ctd;
            ctd.format = ct["format"].asString("swapchain");

            auto blend = ct["blend"];
            if (blend.isObject()) {
                ctd.blend = BlendDef{};
                ctd.blend->srcColor = toSDLBlendFactor(blend["src_color"].asString("src_alpha"));
                ctd.blend->dstColor = toSDLBlendFactor(blend["dst_color"].asString("one_minus_src_alpha"));
                ctd.blend->colorOp = toSDLBlendOp(blend["color_op"].asString("add"));
                ctd.blend->srcAlpha = toSDLBlendFactor(blend["src_alpha"].asString("one"));
                ctd.blend->dstAlpha = toSDLBlendFactor(blend["dst_alpha"].asString("one_minus_src_alpha"));
                ctd.blend->alphaOp = toSDLBlendOp(blend["alpha_op"].asString("add"));
                ctd.blend->colorWriteMask = static_cast<SDL_GPUColorComponentFlags>(blend["color_write_mask"].asInt(0xF));
                ctd.blend->enableBlend = blend["enable_blend"].asBool(true);
                ctd.blend->enableColorWriteMask = blend["enable_color_write_mask"].asBool(false);
            }
            def._colorTargets.push_back(ctd);
        }
    }

    // Multisample
    auto ms = v["multisample"];
    if (ms.isObject()) {
        def._multisample.sampleCount = toSDLSampleCount(ms["sample_count"].asInt(1));
        def._multisample.sampleMask = static_cast<Uint32>(ms["sample_mask"].asInt(0));
        def._multisample.enableAlphaToCoverage = ms["enable_alpha_to_coverage"].asBool(false);
    }

    return def;
}

// =====================================================================
// PipelineDef::dump
// =====================================================================

core::Value PipelineDef::dump() const {
    auto obj = core::Value::object();

    obj.asObject()["vertex_shader"] = core::Value(_vertexShader.toString());
    if (_fragmentShader.has_value()) {
        obj.asObject()["fragment_shader"] = core::Value(_fragmentShader->toString());
    } else {
        obj.asObject()["fragment_shader"] = core::Value();
    }
    obj.asObject()["primitive_type"] = core::Value(fromSDLPrimitiveType(_primitiveType));

    // Vertex input
    {
        auto viObj = core::Value::object();
        auto bufs = core::Value::array();
        for (auto &b : _vertexBuffers) {
            auto bo = core::Value::object();
            bo.asObject()["slot"] = core::Value(static_cast<int>(b.slot));
            bo.asObject()["stride"] = core::Value(static_cast<int>(b.pitch));
            bo.asObject()["input_rate"] = core::Value(fromSDLVertexInputRate(b.input_rate));
            bufs.asArray().push_back(bo);
        }
        auto attrs = core::Value::array();
        for (auto &a : _vertexAttributes) {
            auto ao = core::Value::object();
            ao.asObject()["location"] = core::Value(static_cast<int>(a.location));
            ao.asObject()["buffer_slot"] = core::Value(static_cast<int>(a.buffer_slot));
            ao.asObject()["format"] = core::Value(fromSDLVertexFormat(a.format));
            ao.asObject()["offset"] = core::Value(static_cast<int>(a.offset));
            attrs.asArray().push_back(ao);
        }
        viObj.asObject()["buffers"] = bufs;
        viObj.asObject()["attributes"] = attrs;
        obj.asObject()["vertex_input"] = viObj;
    }

    // Rasterizer
    {
        auto rObj = core::Value::object();
        rObj.asObject()["fill_mode"] = core::Value(fromSDLFillMode(_rasterizer.fillMode));
        rObj.asObject()["cull_mode"] = core::Value(fromSDLCullMode(_rasterizer.cullMode));
        rObj.asObject()["front_face"] = core::Value(fromSDLFrontFace(_rasterizer.frontFace));
        rObj.asObject()["depth_bias_constant_factor"] = core::Value(_rasterizer.depthBiasConstantFactor);
        rObj.asObject()["depth_bias_clamp"] = core::Value(_rasterizer.depthBiasClamp);
        rObj.asObject()["depth_bias_slope_factor"] = core::Value(_rasterizer.depthBiasSlopeFactor);
        rObj.asObject()["enable_depth_bias"] = core::Value(_rasterizer.enableDepthBias);
        rObj.asObject()["enable_depth_clip"] = core::Value(_rasterizer.enableDepthClip);
        obj.asObject()["rasterizer"] = rObj;
    }

    // Depth stencil
    if (_depthStencil.has_value()) {
        auto dsObj = core::Value::object();
        dsObj.asObject()["compare_op"] = core::Value(fromSDLCompareOp(_depthStencil->compareOp));
        dsObj.asObject()["depth_test"] = core::Value(_depthStencil->enableDepthTest);
        dsObj.asObject()["depth_write"] = core::Value(_depthStencil->enableDepthWrite);
        dsObj.asObject()["enable_stencil_test"] = core::Value(_depthStencil->enableStencilTest);
        dsObj.asObject()["compare_mask"] = core::Value(static_cast<int>(_depthStencil->compareMask));
        dsObj.asObject()["write_mask"] = core::Value(static_cast<int>(_depthStencil->writeMask));
        {
            auto bsObj = core::Value::object();
            bsObj.asObject()["fail_op"] = core::Value(fromSDLStencilOp(_depthStencil->backStencil.failOp));
            bsObj.asObject()["pass_op"] = core::Value(fromSDLStencilOp(_depthStencil->backStencil.passOp));
            bsObj.asObject()["depth_fail_op"] = core::Value(fromSDLStencilOp(_depthStencil->backStencil.depthFailOp));
            bsObj.asObject()["compare_op"] = core::Value(fromSDLCompareOp(_depthStencil->backStencil.compareOp));
            dsObj.asObject()["back_stencil"] = bsObj;
        }
        {
            auto fsObj = core::Value::object();
            fsObj.asObject()["fail_op"] = core::Value(fromSDLStencilOp(_depthStencil->frontStencil.failOp));
            fsObj.asObject()["pass_op"] = core::Value(fromSDLStencilOp(_depthStencil->frontStencil.passOp));
            fsObj.asObject()["depth_fail_op"] = core::Value(fromSDLStencilOp(_depthStencil->frontStencil.depthFailOp));
            fsObj.asObject()["compare_op"] = core::Value(fromSDLCompareOp(_depthStencil->frontStencil.compareOp));
            dsObj.asObject()["front_stencil"] = fsObj;
        }
        obj.asObject()["depth_stencil"] = dsObj;
    } else {
        obj.asObject()["depth_stencil"] = core::Value();
    }

    // Color targets
    {
        auto ctsArr = core::Value::array();
        for (auto &ct : _colorTargets) {
            auto ctObj = core::Value::object();
            ctObj.asObject()["format"] = core::Value(ct.format);
            if (ct.blend.has_value()) {
                auto bObj = core::Value::object();
                bObj.asObject()["src_color"] = core::Value(fromSDLBlendFactor(ct.blend->srcColor));
                bObj.asObject()["dst_color"] = core::Value(fromSDLBlendFactor(ct.blend->dstColor));
                bObj.asObject()["color_op"] = core::Value(fromSDLBlendOp(ct.blend->colorOp));
                bObj.asObject()["src_alpha"] = core::Value(fromSDLBlendFactor(ct.blend->srcAlpha));
                bObj.asObject()["dst_alpha"] = core::Value(fromSDLBlendFactor(ct.blend->dstAlpha));
                bObj.asObject()["alpha_op"] = core::Value(fromSDLBlendOp(ct.blend->alphaOp));
                bObj.asObject()["color_write_mask"] = core::Value(static_cast<int>(ct.blend->colorWriteMask));
                bObj.asObject()["enable_blend"] = core::Value(ct.blend->enableBlend);
                bObj.asObject()["enable_color_write_mask"] = core::Value(ct.blend->enableColorWriteMask);
                ctObj.asObject()["blend"] = bObj;
            } else {
                ctObj.asObject()["blend"] = core::Value();
            }
            ctsArr.asArray().push_back(ctObj);
        }
        obj.asObject()["color_targets"] = ctsArr;
    }

    // Multisample
    {
        auto msObj = core::Value::object();
        msObj.asObject()["sample_count"] = core::Value(fromSDLSampleCount(_multisample.sampleCount));
        msObj.asObject()["sample_mask"] = core::Value(static_cast<int>(_multisample.sampleMask));
        msObj.asObject()["enable_alpha_to_coverage"] = core::Value(_multisample.enableAlphaToCoverage);
        obj.asObject()["multisample"] = msObj;
    }

    return obj;
}

// =====================================================================
// PipelineDef::createPipeline
// =====================================================================

SDL_GPUGraphicsPipeline *PipelineDef::createPipeline(
    SDL_GPUDevice *device,
    const std::map<core::NamespacedId, SDL_GPUShader *> &shaderMap,
    SDL_GPUTextureFormat swapchainFormat) const {

    SDL_GPUGraphicsPipelineCreateInfo info{};

    // Shaders
    auto vsIt = shaderMap.find(_vertexShader);
    info.vertex_shader = (vsIt != shaderMap.end()) ? vsIt->second : nullptr;

    if (_fragmentShader.has_value()) {
        auto fsIt = shaderMap.find(*_fragmentShader);
        info.fragment_shader = (fsIt != shaderMap.end()) ? fsIt->second : nullptr;
    } else {
        info.fragment_shader = nullptr;
    }

    // Primitive type
    info.primitive_type = _primitiveType;

    // Vertex input
    info.vertex_input_state.vertex_buffer_descriptions = _vertexBuffers.data();
    info.vertex_input_state.num_vertex_buffers =
        static_cast<Uint32>(_vertexBuffers.size());
    info.vertex_input_state.vertex_attributes = _vertexAttributes.data();
    info.vertex_input_state.num_vertex_attributes =
        static_cast<Uint32>(_vertexAttributes.size());

    // Rasterizer
    info.rasterizer_state.fill_mode = _rasterizer.fillMode;
    info.rasterizer_state.cull_mode = _rasterizer.cullMode;
    info.rasterizer_state.front_face = _rasterizer.frontFace;
    info.rasterizer_state.depth_bias_constant_factor = _rasterizer.depthBiasConstantFactor;
    info.rasterizer_state.depth_bias_clamp = _rasterizer.depthBiasClamp;
    info.rasterizer_state.depth_bias_slope_factor = _rasterizer.depthBiasSlopeFactor;
    info.rasterizer_state.enable_depth_bias = _rasterizer.enableDepthBias;
    info.rasterizer_state.enable_depth_clip = _rasterizer.enableDepthClip;

    // Depth stencil
    if (_depthStencil.has_value()) {
        info.depth_stencil_state.compare_op = _depthStencil->compareOp;
        info.depth_stencil_state.back_stencil_state.fail_op = _depthStencil->backStencil.failOp;
        info.depth_stencil_state.back_stencil_state.pass_op = _depthStencil->backStencil.passOp;
        info.depth_stencil_state.back_stencil_state.depth_fail_op = _depthStencil->backStencil.depthFailOp;
        info.depth_stencil_state.back_stencil_state.compare_op = _depthStencil->backStencil.compareOp;
        info.depth_stencil_state.front_stencil_state.fail_op = _depthStencil->frontStencil.failOp;
        info.depth_stencil_state.front_stencil_state.pass_op = _depthStencil->frontStencil.passOp;
        info.depth_stencil_state.front_stencil_state.depth_fail_op = _depthStencil->frontStencil.depthFailOp;
        info.depth_stencil_state.front_stencil_state.compare_op = _depthStencil->frontStencil.compareOp;
        info.depth_stencil_state.compare_mask = _depthStencil->compareMask;
        info.depth_stencil_state.write_mask = _depthStencil->writeMask;
        info.depth_stencil_state.enable_depth_test = _depthStencil->enableDepthTest;
        info.depth_stencil_state.enable_depth_write = _depthStencil->enableDepthWrite;
        info.depth_stencil_state.enable_stencil_test = _depthStencil->enableStencilTest;
        info.target_info.has_depth_stencil_target = true;
        info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    } else {
        info.target_info.has_depth_stencil_target = false;
    }

    // Color targets
    std::vector<SDL_GPUColorTargetDescription> sdlCTs;
    sdlCTs.reserve(_colorTargets.size());
    for (auto &ct : _colorTargets) {
        SDL_GPUColorTargetDescription d{};
        if (ct.format == "swapchain") {
            d.format = swapchainFormat;
        } else {
            d.format = toSDLTextureFormat(ct.format);
        }
        if (ct.blend.has_value()) {
            d.blend_state.src_color_blendfactor = ct.blend->srcColor;
            d.blend_state.dst_color_blendfactor = ct.blend->dstColor;
            d.blend_state.color_blend_op = ct.blend->colorOp;
            d.blend_state.src_alpha_blendfactor = ct.blend->srcAlpha;
            d.blend_state.dst_alpha_blendfactor = ct.blend->dstAlpha;
            d.blend_state.alpha_blend_op = ct.blend->alphaOp;
            d.blend_state.color_write_mask = ct.blend->colorWriteMask;
            d.blend_state.enable_blend = ct.blend->enableBlend;
            d.blend_state.enable_color_write_mask = ct.blend->enableColorWriteMask;
        }
        sdlCTs.push_back(d);
    }
    info.target_info.num_color_targets = static_cast<Uint32>(sdlCTs.size());
    info.target_info.color_target_descriptions = sdlCTs.data();

    // Multisample
    info.multisample_state.sample_count = _multisample.sampleCount;
    info.multisample_state.sample_mask = _multisample.sampleMask;
    info.multisample_state.enable_alpha_to_coverage = _multisample.enableAlphaToCoverage;

    return SDL_CreateGPUGraphicsPipeline(device, &info);
}

} // namespace noix::video
