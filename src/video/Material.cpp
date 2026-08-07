#include "video/Material.h"
#include "core/Logger.h"

#include <SDL3/SDL.h>

#include <fstream>
#include <sstream>

namespace noix::video {

// ---- Parsing helpers ----

static bool parseTextureBinding(const core::Value &val, TextureBinding &out) {
    if (val.isString()) {
        out.asset = core::NamespacedId::parse(val.asString());
        return true;
    }
    if (val.isObject()) {
        auto assetStr = val["asset"].asString();
        if (assetStr.empty()) return false;
        out.asset = core::NamespacedId::parse(assetStr);
        out.format = val["format"].asString("");
        out.minFilter = val["min_filter"].asString("linear");
        out.magFilter = val["mag_filter"].asString("linear");
        out.addressModeU = val["address_mode_u"].asString("repeat");
        out.addressModeV = val["address_mode_v"].asString("repeat");
        return true;
    }
    return false;
}

static std::optional<MaterialPayload> parsePayload(const core::Value &root) {
    if (!root.isObject()) return std::nullopt;

    MaterialPayload payload;

    for (const auto &[passName, passVal] : root.asObject()) {
        if (!passVal.isObject()) continue;

        PassResources pr;
        auto uniforms = passVal["uniforms"];
        if (uniforms.isObject()) {
            for (const auto &[key, val] : uniforms.asObject()) {
                pr.uniforms[key] = val;
            }
        }

        auto textures = passVal["textures"];
        if (textures.isObject()) {
            for (const auto &[key, val] : textures.asObject()) {
                TextureBinding binding;
                if (!parseTextureBinding(val, binding)) {
                    core::Logger::instance().error(
                        "Material: Invalid texture binding for '{}.{}'", passName, key);
                    continue;
                }
                pr.textures[key] = binding;
            }
        }

        payload.passes[passName] = std::move(pr);
    }

    if (payload.passes.empty()) {
        core::Logger::instance().error("Material: No passes defined");
        return std::nullopt;
    }

    return payload;
}

static std::optional<MaterialPayload> parseFromFile(const std::string &path) {
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
    return parsePayload(root);
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
    std::string jsonStr(reinterpret_cast<const char *>(content.data()),
                        content.size());
    auto root = core::Value::parse(jsonStr);
    if (root.isNull()) return nullptr;
    auto parsed = parsePayload(root);
    if (!parsed.has_value()) return nullptr;
    return std::make_shared<MaterialPayload>(std::move(*parsed));
}

Material::Handle Material::resolve(const core::NamespacedId &id,
                                    std::filesystem::path filePath,
                                    core::ResourceMode mode) {
    MaterialPayloadRef payloadRef;

    if (mode == core::ResourceMode::Dynamic) {
        auto parsed = parseFromFile(filePath.string());
        if (!parsed.has_value()) {
            core::Logger::instance().error("Material: Failed to parse: {} ({})",
                                           id.toString(), filePath.string());
            return {};
        }
        payloadRef = std::make_shared<MaterialPayload>(std::move(*parsed));
    }

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
