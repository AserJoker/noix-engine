#pragma once

/*
 * Nxmd — CPU-side mesh resource (Noix Mesh Data format).
 *
 * Analogous to Image: holds parsed geometry data (vertices, indices,
 * skeleton, animation) in Dynamic mode, or re-reads from disk in Static mode.
 * Supports SlotMap protocol for unified Handle-based access.
 */

#include "core/Handle.h"
#include "core/NamespacedId.h"
#include "core/Resource.h"
#include "core/SlotMap.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace noix::video {

// ---- NXMD format constants ----

constexpr uint32_t NXMD_MAGIC = 0x444D584E; // "NXMD" in little-endian
constexpr uint16_t NXMD_VERSION = 1;

enum SectionType : uint32_t {
    SECTION_VERTEX_BUFFER = 0,
    SECTION_INDEX_BUFFER = 1,
    SECTION_SKELETON = 2,
    SECTION_ANIMATION_CLIP = 3,
};

enum VertexFormat : uint32_t {
    VF_FLOAT2 = 0,
    VF_FLOAT3 = 1,
    VF_FLOAT4 = 2,
    VF_BYTE2_NORM = 3,
    VF_BYTE4_NORM = 4,
    VF_UBYTE2_NORM = 5,
    VF_UBYTE4_NORM = 6,
    VF_SHORT2_NORM = 7,
    VF_SHORT4_NORM = 8,
    VF_USHORT2_NORM = 9,
    VF_USHORT4_NORM = 10,
};

enum IndexType : uint32_t {
    INDEX_UINT16 = 0,
    INDEX_UINT32 = 1,
};

// ---- POD data types ----

struct AttributeDesc {
    uint32_t location;
    uint32_t format; // VertexFormat
    uint32_t offset;
};

struct BoneDesc {
    int32_t parentIndex; // -1 = root
    float inverseBindMatrix[16];
};

struct KeyframeDesc {
    float time;
    float translation[3];
    float rotation[4]; // quaternion (x,y,z,w)
    float scale[3];
};

// ---- Parsed geometry data (value type, returned by accessors) ----

struct NxmdPayload {
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t indexType = INDEX_UINT16;
    std::vector<AttributeDesc> attributes;
    std::optional<std::vector<BoneDesc>> bones;
    std::vector<std::vector<KeyframeDesc>> animationClips;
    std::vector<uint8_t> vertexData;
    std::vector<uint8_t> indexData;
};

/// Reference-counted NxmdPayload. Auto-freed on last reference drop.
using NxmdPayloadRef = std::shared_ptr<NxmdPayload>;

// ---- Nxmd Resource ----

class Nxmd : public core::Resource {
public:
    using Handle = core::Handle<Nxmd>;

    // --- SlotMap protocol ---

    static core::SlotMap<Nxmd> &slotMap() {
        static core::SlotMap<Nxmd> _cache;
        return _cache;
    }

    /// Create Nxmd and insert into SlotMap.
    /// Dynamic: parses from filePath immediately.
    /// Static: stores filePath only, parses on demand.
    static Handle resolve(const core::NamespacedId &id,
                          std::filesystem::path filePath,
                          core::ResourceMode mode = core::ResourceMode::Dynamic);

    /// Create a builtin Nxmd from an existing payload (always Dynamic).
    static Handle create(const core::NamespacedId &id,
                         NxmdPayloadRef payload);

    // --- Data access ---
    // Both modes return NxmdPayloadRef. Caller never manually frees.

    /// Get a reference-counted NxmdPayload.
    /// Dynamic: returns shared reference to held data.
    /// Static:  decodes from disk, returns new shared reference.
    NxmdPayloadRef data() const;

    ~Nxmd() override = default;

    Nxmd(Nxmd &&) = default;
    Nxmd &operator=(Nxmd &&) = default;

    Nxmd(const Nxmd &) = delete;
    Nxmd &operator=(const Nxmd &) = delete;

private:
    Nxmd(const core::NamespacedId &id,
         std::filesystem::path filePath,
         core::ResourceMode mode,
         NxmdPayloadRef payload);

    /// Decode NXMD file from disk into payload (for Static mode).
    NxmdPayloadRef decodePayload() const;

    // Only populated in Dynamic mode. Empty in Static mode.
    mutable NxmdPayloadRef _payloadRef;
};

} // namespace noix::video
