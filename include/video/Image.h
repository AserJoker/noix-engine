#pragma once

/*
 * Image — CPU-side image resource wrapping SDL_Surface.
 *
 * surface() returns SurfaceRef (shared_ptr<SDL_Surface>):
 *   Dynamic: returns shared reference to held surface. Auto-freed when
 *            Image + all refs are gone.
 *   Static:  decodes from disk each call. Auto-freed when last ref drops.
 *   Caller never manually frees — same API for both modes.
 */

#include "core/Handle.h"
#include "core/NamespacedId.h"
#include "core/Resource.h"
#include "core/SlotMap.h"

#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace noix::video {

/// Reference-counted SDL_Surface. Auto-freed on last reference drop.
using SurfaceRef = std::shared_ptr<SDL_Surface>;

class Image : public core::Resource {
public:
    using Handle = core::Handle<Image>;

    // --- SlotMap protocol ---

    static core::SlotMap<Image> &slotMap() {
        static core::SlotMap<Image> _cache;
        return _cache;
    }

    /// Decode bytes and insert into SlotMap.
    static Handle resolve(const core::NamespacedId &id,
                          std::vector<uint8_t> data,
                          std::filesystem::path filePath,
                          core::ResourceMode mode = core::ResourceMode::Dynamic);

    // --- Surface access ---

    /// Get a reference-counted SDL_Surface.
    /// Dynamic: returns shared reference to held surface.
    /// Static:  decodes from disk, returns new shared reference.
    SurfaceRef surface() const;

    ~Image() override = default;

    Image(Image &&) = default;
    Image &operator=(Image &&) = default;

private:
    SurfaceRef decodeSurface() const;
    Image(const core::NamespacedId &id,
          std::filesystem::path filePath,
          core::ResourceMode mode,
          SurfaceRef surface);

    // Only populated in Dynamic mode. Empty in Static mode.
    mutable SurfaceRef _surfaceRef;
};

} // namespace noix::video
