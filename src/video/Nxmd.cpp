#include "video/Nxmd.h"
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
    uint32_t type;
    uint32_t descSize;
    uint64_t dataOffset;
    uint64_t dataSize;
};
#pragma pack(pop)

// ---- Parsing from SDL_IOStream ----

static std::optional<NxmdPayload> parseFromIO(SDL_IOStream *io) {
    FileHeader header{};
    if (SDL_ReadIO(io, &header, sizeof(header)) != sizeof(header)) {
        core::Logger::instance().error("Nxmd: Failed to read header");
        return std::nullopt;
    }

    if (header.magic != NXMD_MAGIC) {
        core::Logger::instance().error("Nxmd: Invalid magic: 0x{:08X}", header.magic);
        return std::nullopt;
    }

    if (header.version > NXMD_VERSION) {
        core::Logger::instance().error("Nxmd: Unsupported version: {}", header.version);
        return std::nullopt;
    }

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
            core::Logger::instance().error("Nxmd: Failed to read section descriptor {}", i);
            return std::nullopt;
        }

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
            SDL_SeekIO(io, sec.desc.dataOffset, SDL_IO_SEEK_SET);
            break;
        }
    }

    NxmdPayload payload;

    for (uint32_t i = 0; i < header.sectionCount; ++i) {
        const auto &sec = sections[i];
        SDL_SeekIO(io, sec.desc.dataOffset, SDL_IO_SEEK_SET);

        switch (sec.desc.type) {
        case SECTION_VERTEX_BUFFER: {
            payload.vertexCount = sec.vertexCount;
            payload.attributes = sec.attributes;

            uint32_t stride = 0;
            if (!sec.attributes.empty()) {
                static const uint32_t formatSizes[] = {8, 12, 16, 2, 4, 2, 4, 4, 8, 4, 8};
                const auto &last = sec.attributes.back();
                stride = last.offset + formatSizes[last.format];
            }
            size_t vDataSize = static_cast<size_t>(sec.vertexCount) * stride;
            payload.vertexData.resize(vDataSize);
            SDL_ReadIO(io, payload.vertexData.data(), vDataSize);
            break;
        }

        case SECTION_INDEX_BUFFER: {
            payload.indexCount = sec.indexCount;
            payload.indexType = sec.indexType;
            size_t iDataSize = static_cast<size_t>(sec.indexCount) *
                               (sec.indexType == INDEX_UINT16 ? 2 : 4);
            payload.indexData.resize(iDataSize);
            SDL_ReadIO(io, payload.indexData.data(), iDataSize);
            break;
        }

        case SECTION_SKELETON: {
            payload.bones.emplace();
            payload.bones->resize(sec.boneCount);
            for (uint32_t j = 0; j < sec.boneCount; ++j) {
                SDL_ReadIO(io, &(*payload.bones)[j], sizeof(BoneDesc));
            }
            break;
        }

        case SECTION_ANIMATION_CLIP: {
            payload.animationClips.resize(sec.boneCount);
            for (uint32_t b = 0; b < sec.boneCount; ++b) {
                uint32_t kfCount = 0;
                SDL_ReadIO(io, &kfCount, 4);
                payload.animationClips[b].resize(kfCount);
                for (uint32_t k = 0; k < kfCount; ++k) {
                    SDL_ReadIO(io, &payload.animationClips[b][k], sizeof(KeyframeDesc));
                }
            }
            break;
        }

        default:
            break;
        }
    }

    return payload;
}

static std::optional<NxmdPayload> parseFromFile(const std::string &path) {
    auto *io = SDL_IOFromFile(path.c_str(), "rb");
    if (!io) {
        core::Logger::instance().error("Nxmd: Cannot open file: {}", path);
        return std::nullopt;
    }
    auto result = parseFromIO(io);
    SDL_CloseIO(io);
    return result;
}

// ---- Nxmd Resource ----

Nxmd::Nxmd(const core::NamespacedId &id,
           std::filesystem::path filePath,
           core::ResourceMode mode,
           NxmdPayloadRef payload)
    : core::Resource(id, std::move(filePath), mode),
      _payloadRef(std::move(payload)) {}

NxmdPayloadRef Nxmd::decodePayload() const {
    auto parsed = parseFromFile(filePath().string());
    if (!parsed.has_value()) return nullptr;
    return std::make_shared<NxmdPayload>(std::move(*parsed));
}

Nxmd::Handle Nxmd::resolve(const core::NamespacedId &id,
                            std::filesystem::path filePath,
                            core::ResourceMode mode) {
    NxmdPayloadRef payloadRef;

    if (mode == core::ResourceMode::Dynamic) {
        auto parsed = parseFromFile(filePath.string());
        if (!parsed.has_value()) {
            core::Logger::instance().error("Nxmd: Failed to parse: {} ({})",
                                           id.toString(), filePath.string());
            return {};
        }
        payloadRef = std::make_shared<NxmdPayload>(std::move(*parsed));
    }
    // Static: payloadRef stays empty, decoded on demand

    Nxmd nxmd(id, std::move(filePath), mode, std::move(payloadRef));
    return Handle(slotMap().insert(std::move(nxmd)));
}

Nxmd::Handle Nxmd::create(const core::NamespacedId &id,
                           NxmdPayloadRef payload) {
    Nxmd nxmd(id, "", core::ResourceMode::Dynamic, std::move(payload));
    return Handle(slotMap().insert(std::move(nxmd)));
}

NxmdPayloadRef Nxmd::data() const {
    if (mode() == core::ResourceMode::Dynamic) {
        return _payloadRef;
    }
    return decodePayload();
}

} // namespace noix::video
