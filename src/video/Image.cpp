#include "video/Image.h"
#include "core/Logger.h"

namespace noix::video {

struct SurfaceDeleter {
    void operator()(SDL_Surface *s) const {
        if (s) SDL_DestroySurface(s);
    }
};

Image::Image(const core::NamespacedId &id,
             std::filesystem::path filePath,
             core::ResourceMode mode,
             SurfaceRef surface)
    : core::Resource(id, std::move(filePath), mode),
      _surfaceRef(std::move(surface)) {}

Image::Handle Image::resolve(const core::NamespacedId &id,
                              std::vector<uint8_t> data,
                              std::filesystem::path filePath,
                              core::ResourceMode mode) {
    SurfaceRef surfaceRef;

    if (mode == core::ResourceMode::Dynamic) {
        auto *io = SDL_IOFromConstMem(data.data(), static_cast<size_t>(data.size()));
        if (!io) {
            core::Logger::instance().error("Image: Failed to create IO stream");
            return {};
        }
        SDL_Surface *surface = IMG_Load_IO(io, true);
        if (!surface) {
            core::Logger::instance().error("Image: Failed to decode: {}",
                                           SDL_GetError());
            return {};
        }
        surfaceRef = SurfaceRef(surface, SurfaceDeleter{});
    }
    // Static: surfaceRef stays empty, decoded on demand

    Image img(id, std::move(filePath), mode, std::move(surfaceRef));
    return Handle(slotMap().insert(std::move(img)));
}

SurfaceRef Image::surface() const {
    if (mode() == core::ResourceMode::Dynamic) {
        return _surfaceRef;
    }
    return decodeSurface();
}

SurfaceRef Image::decodeSurface() const {
    SDL_Surface *raw = IMG_Load(filePath().string().c_str());
    if (!raw) {
        core::Logger::instance().error("Image: Failed to decode from disk: {}",
                                       filePath().string());
        return nullptr;
    }
    return SurfaceRef(raw, SurfaceDeleter{});
}

} // namespace noix::video
