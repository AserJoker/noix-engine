#include "video/Material.h"
#include "core/Logger.h"

#include <SDL3/SDL.h>

#include <fstream>
#include <sstream>

namespace noix::video {

// ---- Parsing ----

static std::optional<MaterialPayload> parseFromPath(const std::string &path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        core::Logger::instance().error("Material: Cannot open file: {}", path);
        return std::nullopt;
    }
    std::stringstream buf;
    buf << file.rdbuf();
    auto root = core::Value::parse(buf.str());
    if (root.isNull()) {
        core::Logger::instance().error("Material: Cannot parse file: {}", path);
        return std::nullopt;
    }

    if (!root.isObject()) {
        core::Logger::instance().error("Material: Root is not an object");
        return std::nullopt;
    }

    MaterialPayload payload;

    auto pipelineStr = root["pipeline"].asString();
    if (pipelineStr.empty()) {
        core::Logger::instance().error("Material: Missing 'pipeline' field");
        return std::nullopt;
    }
    payload.pipeline = core::NamespacedId::parse(pipelineStr);

    auto uniforms = root["uniforms"];
    if (uniforms.isObject()) {
        for (const auto &[key, val] : uniforms.asObject()) {
            payload.uniforms[key] = val;
        }
    }

    auto textures = root["textures"];
    if (textures.isObject()) {
        for (const auto &[key, val] : textures.asObject()) {
            TextureBinding binding;

            if (val.isString()) {
                binding.asset = core::NamespacedId::parse(val.asString());
            } else if (val.isObject()) {
                auto assetStr = val["asset"].asString();
                if (assetStr.empty()) {
                    core::Logger::instance().error(
                        "Material: Texture '{}' missing 'asset' field", key);
                    continue;
                }
                binding.asset = core::NamespacedId::parse(assetStr);
                binding.format = val["format"].asString("");
                binding.minFilter = val["min_filter"].asString("linear");
                binding.magFilter = val["mag_filter"].asString("linear");
                binding.addressModeU = val["address_mode_u"].asString("repeat");
                binding.addressModeV = val["address_mode_v"].asString("repeat");
            } else {
                core::Logger::instance().error(
                    "Material: Invalid texture binding for '{}'", key);
                continue;
            }

            payload.textures[key] = binding;
        }
    }

    return payload;
}

// ---- Material Resource ----

Material::Material(const core::NamespacedId &id,
                   std::filesystem::path filePath,
                   core::ResourceMode mode,
                   MaterialPayloadRef payload)
    : core::Resource(id, std::move(filePath), mode),
      _payload(std::move(payload)) {}

MaterialPayloadRef Material::decodePayload() const {
    auto content = readFileContent();
    if (content.empty()) return nullptr;
    // Parse from the read content
    std::string jsonStr(reinterpret_cast<const char *>(content.data()), content.size());
    auto root = core::Value::parse(jsonStr);
    if (root.isNull()) return nullptr;
    // Reuse parseFromPath logic by writing to temp and parsing...
    // Actually, just parse inline:
    if (!root.isObject()) return nullptr;

    MaterialPayload payload;
    auto pipelineStr = root["pipeline"].asString();
    if (pipelineStr.empty()) return nullptr;
    payload.pipeline = core::NamespacedId::parse(pipelineStr);

    auto uniforms = root["uniforms"];
    if (uniforms.isObject()) {
        for (const auto &[key, val] : uniforms.asObject()) {
            payload.uniforms[key] = val;
        }
    }

    auto textures = root["textures"];
    if (textures.isObject()) {
        for (const auto &[key, val] : textures.asObject()) {
            TextureBinding binding;
            if (val.isString()) {
                binding.asset = core::NamespacedId::parse(val.asString());
            } else if (val.isObject()) {
                auto assetStr = val["asset"].asString();
                if (assetStr.empty()) continue;
                binding.asset = core::NamespacedId::parse(assetStr);
                binding.format = val["format"].asString("");
                binding.minFilter = val["min_filter"].asString("linear");
                binding.magFilter = val["mag_filter"].asString("linear");
                binding.addressModeU = val["address_mode_u"].asString("repeat");
                binding.addressModeV = val["address_mode_v"].asString("repeat");
            } else {
                continue;
            }
            payload.textures[key] = binding;
        }
    }

    return std::make_shared<MaterialPayload>(std::move(payload));
}

Material::Handle Material::resolve(const core::NamespacedId &id,
                                    std::filesystem::path filePath,
                                    core::ResourceMode mode) {
    MaterialPayloadRef payloadRef;

    if (mode == core::ResourceMode::Dynamic) {
        auto parsed = parseFromPath(filePath.string());
        if (!parsed.has_value()) {
            core::Logger::instance().error("Material: Failed to parse: {} ({})",
                                           id.toString(), filePath.string());
            return {};
        }
        payloadRef = std::make_shared<MaterialPayload>(std::move(*parsed));
    }
    // Static: payloadRef stays empty, decoded on demand

    Material mat(id, std::move(filePath), mode, std::move(payloadRef));
    return Handle(slotMap().insert(std::move(mat)));
}

Material::Handle Material::create(const core::NamespacedId &id,
                                   MaterialPayloadRef payload) {
    Material mat(id, "", core::ResourceMode::Dynamic, std::move(payload));
    return Handle(slotMap().insert(std::move(mat)));
}

MaterialPayloadRef Material::data() const {
    if (mode() == core::ResourceMode::Dynamic) {
        return _payload;
    }
    return decodePayload();
}

} // namespace noix::video
