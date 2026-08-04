#include "video/NxmdData.h"
#include "core/Logger.h"

#include <SDL3/SDL.h>

namespace noix::video {

// ---- Binary layout helpers ----

#pragma pack(push, 1)
struct FileHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t sectionCount;
    uint32_t reserved[3];
};

struct SectionDescriptor {
    uint32_t type;      // SectionType
    uint32_t descSize;  // total size of this descriptor (including type + descSize)
    uint64_t dataOffset;
    uint64_t dataSize;
};
#pragma pack(pop)

std::optional<NxmdData> NxmdData::load(const std::string &path) {
    SDL_IOStream *io = SDL_IOFromFile(path.c_str(), "rb");
    if (!io) {
        core::Logger::instance().error("NxmdData: Cannot open file: {}", path);
        return std::nullopt;
    }

    // Read header
    FileHeader header{};
    if (SDL_ReadIO(io, &header, sizeof(header)) != sizeof(header)) {
        core::Logger::instance().error("NxmdData: Failed to read header");
        SDL_CloseIO(io);
        return std::nullopt;
    }

    if (header.magic != NXMD_MAGIC) {
        core::Logger::instance().error("NxmdData: Invalid magic: 0x{:08X}", header.magic);
        SDL_CloseIO(io);
        return std::nullopt;
    }

    if (header.version > NXMD_VERSION) {
        core::Logger::instance().error("NxmdData: Unsupported version: {}", header.version);
        SDL_CloseIO(io);
        return std::nullopt;
    }

    // Read section descriptors + their type-specific fields
    struct SectionInfo {
        SectionDescriptor desc;
        uint32_t vertexCount = 0;
        uint32_t attributeCount = 0;
        std::vector<AttributeDesc> attributes;
        uint32_t indexCount = 0;
        uint32_t indexType = 0;
        uint32_t boneCount = 0;
        uint32_t frameCount = 0;
        float duration = 0.0f;
    };

    std::vector<SectionInfo> sections(header.sectionCount);
    for (uint32_t i = 0; i < header.sectionCount; ++i) {
        auto &sec = sections[i];
        if (SDL_ReadIO(io, &sec.desc, sizeof(SectionDescriptor)) !=
            sizeof(SectionDescriptor)) {
            core::Logger::instance().error("NxmdData: Failed to read section descriptor {}", i);
            SDL_CloseIO(io);
            return std::nullopt;
        }

        // Read type-specific fields based on section type
        switch (sec.desc.type) {
        case SECTION_VERTEX_BUFFER:
            SDL_ReadIO(io, &sec.vertexCount, 4);
            SDL_ReadIO(io, &sec.attributeCount, 4);
            sec.attributes.resize(sec.attributeCount);
            for (uint32_t j = 0; j < sec.attributeCount; ++j) {
                SDL_ReadIO(io, &sec.attributes[j], sizeof(AttributeDesc));
            }
            break;
        case SECTION_INDEX_BUFFER:
            SDL_ReadIO(io, &sec.indexCount, 4);
            SDL_ReadIO(io, &sec.indexType, 4);
            break;
        case SECTION_SKELETON:
            SDL_ReadIO(io, &sec.boneCount, 4);
            break;
        case SECTION_ANIMATION_CLIP:
            SDL_ReadIO(io, &sec.boneCount, 4);
            SDL_ReadIO(io, &sec.frameCount, 4);
            SDL_ReadIO(io, &sec.duration, 4);
            break;
        default:
            // Skip unknown type-specific fields
            SDL_SeekIO(io, sec.desc.dataOffset, SDL_IO_SEEK_SET);
            break;
        }
    }

    NxmdData data;

    // Process each section — read raw data
    for (uint32_t i = 0; i < header.sectionCount; ++i) {
        const auto &sec = sections[i];
        SDL_SeekIO(io, sec.desc.dataOffset, SDL_IO_SEEK_SET);

        switch (sec.desc.type) {
        case SECTION_VERTEX_BUFFER: {
            data._vertexCount = sec.vertexCount;
            data._attributes = sec.attributes;

            uint32_t stride = 0;
            if (!sec.attributes.empty()) {
                static const uint32_t formatSizes[] = {8,12,16,2,4,2,4,4,8,4,8};
                const auto &last = sec.attributes.back();
                stride = last.offset + formatSizes[last.format];
            }
            size_t vDataSize = static_cast<size_t>(sec.vertexCount) * stride;
            data._vertexData.resize(vDataSize);
            SDL_ReadIO(io, data._vertexData.data(), vDataSize);
            break;
        }

        case SECTION_INDEX_BUFFER: {
            data._indexCount = sec.indexCount;
            data._indexType = sec.indexType;
            size_t iDataSize = static_cast<size_t>(sec.indexCount) *
                               (sec.indexType == INDEX_UINT16 ? 2 : 4);
            data._indexData.resize(iDataSize);
            SDL_ReadIO(io, data._indexData.data(), iDataSize);
            break;
        }

        case SECTION_SKELETON: {
            data._bones.emplace();
            data._bones->resize(sec.boneCount);
            for (uint32_t j = 0; j < sec.boneCount; ++j) {
                SDL_ReadIO(io, &(*data._bones)[j], sizeof(BoneDesc));
            }
            break;
        }

        case SECTION_ANIMATION_CLIP: {
            data._animationClips.resize(sec.boneCount);
            for (uint32_t b = 0; b < sec.boneCount; ++b) {
                uint32_t kfCount = 0;
                SDL_ReadIO(io, &kfCount, 4);
                data._animationClips[b].resize(kfCount);
                for (uint32_t k = 0; k < kfCount; ++k) {
                    SDL_ReadIO(io, &data._animationClips[b][k], sizeof(KeyframeDesc));
                }
            }
            break;
        }

        default:
            break;
        }
    }

    SDL_CloseIO(io);
    return data;
}

const std::vector<KeyframeDesc> &NxmdData::animationTracks(uint32_t boneIndex) const {
    static const std::vector<KeyframeDesc> empty;
    if (boneIndex >= _animationClips.size()) return empty;
    return _animationClips[boneIndex];
}

} // namespace noix::video
