#pragma once

/*
 * Material — CPU-side material resource.
 * Analogous to Nxmd: holds pipeline reference, uniform values, and texture bindings.
 * No GPU resources — purely descriptive data consumed by the renderer.
 * Supports SlotMap protocol for unified Handle-based access.
 */

#include "core/Handle.h"
#include "core/NamespacedId.h"
#include "core/Resource.h"
#include "core/SlotMap.h"
#include "core/Value.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace noix::video {

// ---- Texture binding ----

struct TextureBinding {
    core::NamespacedId asset;
    // Optional texture format override (e.g. "r8g8b8a8_unorm").
    std::string format;
    // Sampler defaults
    std::string minFilter = "linear";
    std::string magFilter = "linear";
    std::string addressModeU = "repeat";
    std::string addressModeV = "repeat";
};

// ---- Parsed material data ----

struct MaterialPayload {
    core::NamespacedId pipeline;
    std::map<std::string, core::Value> uniforms;
    std::map<std::string, TextureBinding> textures;
};

/// Reference-counted MaterialPayload. Auto-freed on last reference drop.
using MaterialPayloadRef = std::shared_ptr<MaterialPayload>;

// ---- Material Resource ----

class Material : public core::Resource {
public:
    using Handle = core::Handle<Material>;

    // --- SlotMap protocol ---

    static core::SlotMap<Material> &slotMap() {
        static core::SlotMap<Material> _cache;
        return _cache;
    }

    /// Create Material and insert into SlotMap.
    /// Dynamic: parses from filePath immediately.
    /// Static: stores filePath only, parses on demand.
    static Handle resolve(const core::NamespacedId &id,
                          std::filesystem::path filePath,
                          core::ResourceMode mode = core::ResourceMode::Dynamic);

    /// Create a builtin Material from an existing payload (always Dynamic).
    static Handle create(const core::NamespacedId &id,
                         MaterialPayloadRef payload);

    // --- Data access ---

    /// Get a reference-counted MaterialPayload.
    /// Dynamic: returns shared reference to held data.
    /// Static:  decodes from disk, returns new shared reference.
    MaterialPayloadRef data() const;

    ~Material() override = default;

    Material(Material &&) = default;
    Material &operator=(Material &&) = default;

    Material(const Material &) = delete;
    Material &operator=(const Material &) = delete;

private:
    Material(const core::NamespacedId &id,
             std::filesystem::path filePath,
             core::ResourceMode mode,
             MaterialPayloadRef payload);

    /// Decode material JSON from disk (for Static mode).
    MaterialPayloadRef decodePayload() const;

    MaterialPayloadRef _payload;
};

} // namespace noix::video
