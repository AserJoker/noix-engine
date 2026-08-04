#pragma once

/*
 * MaterialDef — Material definition loaded from JSON.
 * References a PipelineDef and provides uniform values + texture bindings.
 */

#include "core/NamespacedId.h"
#include "core/Value.h"

#include <map>
#include <optional>
#include <string>

namespace noix::video {

// ---- Texture binding ----

struct TextureBinding {
    core::NamespacedId asset;
    // Sampler defaults: min_filter=linear, mag_filter=linear,
    //                   address_mode_u=repeat, address_mode_v=repeat
    std::string minFilter = "linear";
    std::string magFilter = "linear";
    std::string addressModeU = "repeat";
    std::string addressModeV = "repeat";
};

// ---- MaterialDef ----

class MaterialDef {
public:
    /// Parse a material JSON file. Returns nullopt on failure.
    static std::optional<MaterialDef> load(const std::string &path);

    /// Serialize to JSON string.
    core::Value dump() const;

    const core::NamespacedId &pipeline() const { return _pipeline; }
    const std::map<std::string, core::Value> &uniforms() const { return _uniforms; }
    const std::map<std::string, TextureBinding> &textures() const { return _textures; }

private:
    core::NamespacedId _pipeline;
    std::map<std::string, core::Value> _uniforms;
    std::map<std::string, TextureBinding> _textures;
};

} // namespace noix::video
