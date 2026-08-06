#pragma once

/*
 * Shader — CPU-side shader resource wrapping SPIR-V byte data.
 *
 * data() returns SpirvRef (shared_ptr<vector<uint8_t>>):
 *   Dynamic: returns shared reference to held data. Auto-freed when
 *            Shader + all refs are gone.
 *   Static:  reads from disk each call. Auto-freed when last ref drops.
 *   Caller never manually frees — same API for both modes.
 *
 * No GPU objects — purely descriptive data consumed by Pipeline.
 */

#include "core/Handle.h"
#include "core/NamespacedId.h"
#include "core/Resource.h"
#include "core/SlotMap.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace noix::video {

/// Reference-counted SPIR-V data. Auto-freed on last reference drop.
using SpirvRef = std::shared_ptr<std::vector<uint8_t>>;

class Shader : public core::Resource {
public:
    using Handle = core::Handle<Shader>;

    // --- SlotMap protocol ---

    static core::SlotMap<Shader> &slotMap() {
        static core::SlotMap<Shader> _cache;
        return _cache;
    }

    /// Create Shader and insert into SlotMap.
    /// Dynamic: reads SPIR-V from file immediately.
    /// Static: stores filePath only, reads on demand.
    static Handle resolve(const core::NamespacedId &id,
                          std::filesystem::path filePath,
                          core::ResourceMode mode = core::ResourceMode::Dynamic);

    /// Create a builtin Shader from existing SPIR-V data (always Dynamic).
    static Handle create(const core::NamespacedId &id,
                         SpirvRef spirvData);

    // --- Data access ---

    /// Get a reference-counted SPIR-V data.
    /// Dynamic: returns shared reference to held data.
    /// Static:  reads from disk, returns new shared reference.
    SpirvRef data() const;

    ~Shader() override = default;

    Shader(Shader &&) = default;
    Shader &operator=(Shader &&) = default;

    Shader(const Shader &) = delete;
    Shader &operator=(const Shader &) = delete;

private:
    Shader(const core::NamespacedId &id,
           std::filesystem::path filePath,
           core::ResourceMode mode,
           SpirvRef spirvData);

    SpirvRef decodeSpirv() const;

    // Only populated in Dynamic mode. Empty in Static mode.
    mutable SpirvRef _spirvData;
};

} // namespace noix::video
