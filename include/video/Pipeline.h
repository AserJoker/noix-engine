#pragma once

/*
 * Pipeline — GPU graphics pipeline resource (SDL_GPUGraphicsPipeline).
 * Loads pipeline definition from JSON, creates shaders and GPU pipeline.
 * Destructor releases GPU object via Application singleton.
 * Supports SlotMap protocol for unified Handle-based access.
 */

#include "core/Handle.h"
#include "core/NamespacedId.h"
#include "core/Resource.h"
#include "core/SlotMap.h"

#include <SDL3/SDL_gpu.h>

#include <filesystem>
#include <vector>

namespace noix::video {

class Pipeline : public core::Resource {
public:
    using Handle = core::Handle<Pipeline>;

    // --- SlotMap protocol ---

    static core::SlotMap<Pipeline> &slotMap() {
        static core::SlotMap<Pipeline> _cache;
        return _cache;
    }

    /// Load a pipeline definition from JSON file and create GPU pipeline.
    /// Shaders are loaded internally via AssetManager.
    static Handle resolve(const core::NamespacedId &id,
                          std::filesystem::path filePath,
                          core::ResourceMode mode,
                          SDL_GPUTextureFormat format);

    /// Create a builtin Pipeline from JSON data (always Dynamic).
    static Handle create(const core::NamespacedId &id,
                         std::vector<uint8_t> jsonData,
                         SDL_GPUTextureFormat format);

    // --- Accessors ---

    SDL_GPUGraphicsPipeline *gpuPipeline() const { return _pipeline; }

    ~Pipeline() override;

    Pipeline(Pipeline &&other) noexcept;
    Pipeline &operator=(Pipeline &&other) noexcept;

    Pipeline(const Pipeline &) = delete;
    Pipeline &operator=(const Pipeline &) = delete;

private:
    Pipeline(const core::NamespacedId &id,
             std::filesystem::path filePath,
             core::ResourceMode mode,
             SDL_GPUGraphicsPipeline *pipeline);

    SDL_GPUGraphicsPipeline *_pipeline = nullptr;
};

} // namespace noix::video
