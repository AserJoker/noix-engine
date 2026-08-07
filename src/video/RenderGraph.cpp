#include "video/RenderGraph.h"
#include "core/Logger.h"
#include "core/Value.h"

#include <SDL3/SDL_gpu.h>

#include <fstream>
#include <sstream>

namespace noix::video {

// ---- Format string → SDL_GPUTextureFormat ----

static std::optional<SDL_GPUTextureFormat> parseTextureFormat(const std::string &s) {
    if (s.empty()) return std::nullopt;
    if (s == "r8g8b8a8_unorm")      return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    if (s == "b8g8r8a8_unorm")      return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    if (s == "r8_unorm")            return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    if (s == "r8g8_unorm")          return SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
    if (s == "r8g8b8a8_snorm")      return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM;
    if (s == "r16_float")           return SDL_GPU_TEXTUREFORMAT_R16_FLOAT;
    if (s == "r16g16_float")        return SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
    if (s == "r16g16b16a16_float")  return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    if (s == "r32_float")           return SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    if (s == "r32g32_float")        return SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
    if (s == "r32g32b32a32_float")  return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    core::Logger::instance().warn("RenderGraph: Unknown texture format '{}'", s);
    return std::nullopt;
}

static RenderPassDef::Sort parseSortMode(const std::string &s) {
    if (s == "back_to_front")   return RenderPassDef::BackToFront;
    if (s == "front_to_back")   return RenderPassDef::FrontToBack;
    return RenderPassDef::None;
}

// ---- fromFile ----

std::optional<RenderGraph> RenderGraph::fromFile(const std::filesystem::path &path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        core::Logger::instance().error("RenderGraph: Cannot open file: {}", path.string());
        return std::nullopt;
    }
    std::stringstream buf;
    buf << file.rdbuf();
    auto root = core::Value::parse(buf.str());
    if (root.isNull() || !root.isObject()) {
        core::Logger::instance().error("RenderGraph: Invalid JSON in {}", path.string());
        return std::nullopt;
    }

    RenderGraph graph;

    // Parse "textures" section
    auto texturesVal = root["textures"];
    if (texturesVal.isObject()) {
        for (const auto &[name, val] : texturesVal.asObject()) {
            RenderTextureDef def;
            def.name = name;

            auto fmtStr = val["format"].asString("r8g8b8a8_unorm");
            auto fmtOpt = parseTextureFormat(fmtStr);
            def.format = fmtOpt.value_or(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);

            auto sizeVal = val["size"];
            if (sizeVal.isArray() && sizeVal.asArray().size() >= 2) {
                def.sizeMode = RenderTextureDef::Fixed;
                def.fixedWidth = static_cast<uint32_t>(sizeVal.asArray()[0].asInt());
                def.fixedHeight = static_cast<uint32_t>(sizeVal.asArray()[1].asInt());
            } else {
                def.sizeMode = RenderTextureDef::Window;
            }

            graph._textures.push_back(std::move(def));
        }
    }

    // Parse "passes" section
    auto passesVal = root["passes"];
    if (!passesVal.isArray()) {
        core::Logger::instance().error("RenderGraph: Missing or invalid 'passes' array");
        return std::nullopt;
    }

    for (const auto &passVal : passesVal.asArray()) {
        RenderPassDef def;
        def.name = passVal["name"].asString();
        if (def.name.empty()) {
            core::Logger::instance().error("RenderGraph: Pass missing 'name'");
            return std::nullopt;
        }

        auto pipelineStr = passVal["pipeline"].asString();
        if (pipelineStr.empty()) {
            core::Logger::instance().error("RenderGraph: Pass '{}' missing 'pipeline'", def.name);
            return std::nullopt;
        }
        def.pipeline = core::NamespacedId::parse(pipelineStr);

        def.target = passVal["target"].asString("swapchain");
        def.sort = parseSortMode(passVal["sort"].asString("none"));

        graph._passes.push_back(std::move(def));
    }

    if (graph._passes.empty()) {
        core::Logger::instance().error("RenderGraph: No passes defined");
        return std::nullopt;
    }

    return graph;
}

} // namespace noix::video
