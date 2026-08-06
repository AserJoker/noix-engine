#include "video/Shader.h"
#include "core/Logger.h"

#include <SDL3/SDL.h>

namespace noix::video {

Shader::Shader(const core::NamespacedId &id,
               std::filesystem::path filePath,
               core::ResourceMode mode,
               SpirvRef spirvData)
    : core::Resource(id, std::move(filePath), mode),
      _spirvData(std::move(spirvData)) {}

Shader::Handle Shader::resolve(const core::NamespacedId &id,
                                std::filesystem::path filePath,
                                core::ResourceMode mode) {
    SpirvRef spirvRef;

    if (mode == core::ResourceMode::Dynamic) {
        size_t size = 0;
        void *raw = SDL_LoadFile(filePath.string().c_str(), &size);
        if (!raw || size == 0) {
            core::Logger::instance().error(
                "Shader: Failed to read: {}", filePath.string());
            return {};
        }
        spirvRef = std::make_shared<std::vector<uint8_t>>(
            static_cast<const uint8_t *>(raw),
            static_cast<const uint8_t *>(raw) + size);
        SDL_free(raw);
    }
    // Static: spirvRef stays empty, decoded on demand

    Shader shader(id, std::move(filePath), mode, std::move(spirvRef));
    return Handle(slotMap().insert(std::move(shader)));
}

Shader::Handle Shader::create(const core::NamespacedId &id,
                               SpirvRef spirvData) {
    if (!spirvData || spirvData->empty()) return {};
    Shader shader(id, "", core::ResourceMode::Dynamic, std::move(spirvData));
    return Handle(slotMap().insert(std::move(shader)));
}

SpirvRef Shader::data() const {
    if (mode() == core::ResourceMode::Dynamic) {
        return _spirvData;
    }
    return decodeSpirv();
}

SpirvRef Shader::decodeSpirv() const {
    size_t size = 0;
    void *raw = SDL_LoadFile(filePath().string().c_str(), &size);
    if (!raw || size == 0) {
        core::Logger::instance().error(
            "Shader: Failed to read from disk: {}", filePath().string());
        return nullptr;
    }
    auto ref = std::make_shared<std::vector<uint8_t>>(
        static_cast<const uint8_t *>(raw),
        static_cast<const uint8_t *>(raw) + size);
    SDL_free(raw);
    return ref;
}

} // namespace noix::video
