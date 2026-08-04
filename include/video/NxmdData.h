#pragma once

/*
 * NxmdData — Noix Mesh Data format.
 * Binary geometry format with self-describing section descriptors.
 */

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <optional>
#include <string>
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

// ---- Parsed NXMD file ----

class NxmdData {
public:
    /// Parse an NXMD file. Returns nullopt on failure.
    static std::optional<NxmdData> load(const std::string &path);

    uint32_t vertexCount() const { return _vertexCount; }
    uint32_t indexCount() const { return _indexCount; }
    const std::vector<AttributeDesc> &attributes() const { return _attributes; }
    uint32_t indexType() const { return _indexType; }
    const std::optional<std::vector<BoneDesc>> &bones() const { return _bones; }
    const std::vector<KeyframeDesc> &animationTracks(uint32_t boneIndex) const;

    const uint8_t *vertexData() const { return _vertexData.data(); }
    size_t vertexDataSize() const { return _vertexData.size(); }
    const uint8_t *indexData() const { return _indexData.data(); }
    size_t indexDataSize() const { return _indexData.size(); }

private:
    uint32_t _vertexCount = 0;
    uint32_t _indexCount = 0;
    uint32_t _indexType = INDEX_UINT16;
    std::vector<AttributeDesc> _attributes;
    std::optional<std::vector<BoneDesc>> _bones;
    std::vector<std::vector<KeyframeDesc>> _animationClips;

    std::vector<uint8_t> _vertexData;
    std::vector<uint8_t> _indexData;
};

} // namespace noix::video
