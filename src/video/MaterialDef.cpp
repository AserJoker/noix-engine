#include "video/MaterialDef.h"
#include "core/Logger.h"

namespace noix::video {

std::optional<MaterialDef> MaterialDef::load(const std::string &path) {
    // Read file
    auto fileContent = core::Value::parse(path);
    if (fileContent.isNull()) {
        core::Logger::instance().error("MaterialDef: Cannot parse file: {}", path);
        return std::nullopt;
    }

    if (!fileContent.isObject()) {
        core::Logger::instance().error("MaterialDef: Root is not an object");
        return std::nullopt;
    }

    MaterialDef mat;

    // Pipeline (required)
    auto pipelineStr = fileContent["pipeline"].asString();
    if (pipelineStr.empty()) {
        core::Logger::instance().error("MaterialDef: Missing 'pipeline' field");
        return std::nullopt;
    }
    mat._pipeline = core::NamespacedId::parse(pipelineStr);

    // Uniforms
    auto uniforms = fileContent["uniforms"];
    if (uniforms.isObject()) {
        for (const auto &[key, val] : uniforms.asObject()) {
            mat._uniforms[key] = val;
        }
    }

    // Textures
    auto textures = fileContent["textures"];
    if (textures.isObject()) {
        for (const auto &[key, val] : textures.asObject()) {
            TextureBinding binding;

            if (val.isString()) {
                // Shorthand: "u_albedo": "noix:textures/metal"
                binding.asset = core::NamespacedId::parse(val.asString());
            } else if (val.isObject()) {
                // Full form: "u_albedo": { "asset": "...", "min_filter": "linear", ... }
                auto assetStr = val["asset"].asString();
                if (assetStr.empty()) {
                    core::Logger::instance().error(
                        "MaterialDef: Texture '{}' missing 'asset' field", key);
                    continue;
                }
                binding.asset = core::NamespacedId::parse(assetStr);
                binding.minFilter = val["min_filter"].asString("linear");
                binding.magFilter = val["mag_filter"].asString("linear");
                binding.addressModeU = val["address_mode_u"].asString("repeat");
                binding.addressModeV = val["address_mode_v"].asString("repeat");
            } else {
                core::Logger::instance().error(
                    "MaterialDef: Invalid texture binding for '{}'", key);
                continue;
            }

            mat._textures[key] = binding;
        }
    }

    return mat;
}

core::Value MaterialDef::dump() const {
    auto obj = core::Value::object();

    obj.asObject()["pipeline"] = core::Value(_pipeline.toString());

    // Uniforms
    auto uniObj = core::Value::object();
    for (const auto &[key, val] : _uniforms) {
        uniObj.asObject()[key] = val;
    }
    obj.asObject()["uniforms"] = uniObj;

    // Textures
    auto texObj = core::Value::object();
    for (const auto &[key, binding] : _textures) {
        auto bObj = core::Value::object();
        bObj.asObject()["asset"] = core::Value(binding.asset.toString());
        bObj.asObject()["min_filter"] = core::Value(binding.minFilter);
        bObj.asObject()["mag_filter"] = core::Value(binding.magFilter);
        bObj.asObject()["address_mode_u"] = core::Value(binding.addressModeU);
        bObj.asObject()["address_mode_v"] = core::Value(binding.addressModeV);
        texObj.asObject()[key] = bObj;
    }
    obj.asObject()["textures"] = texObj;

    return obj;
}

} // namespace noix::video
