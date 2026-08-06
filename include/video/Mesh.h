#pragma once

/*
 * Mesh — GPU mesh resource (vertex/index buffers).
 * Analogous to Texture: consumes Nxmd (CPU) data, uploads to GPU.
 * Destructor releases GPU buffers via Application singleton.
 * Supports SlotMap protocol for unified Handle-based access.
 */

#include "core/Handle.h"
#include "core/NamespacedId.h"
#include "core/Resource.h"
#include "core/SlotMap.h"
#include "video/Nxmd.h"

#include <SDL3/SDL_gpu.h>

#include <filesystem>
#include <vector>

namespace noix::video {

class Mesh : public core::Resource {
public:
    using Handle = core::Handle<Mesh>;

    // --- SlotMap protocol ---

    static core::SlotMap<Mesh> &slotMap() {
        static core::SlotMap<Mesh> _cache;
        return _cache;
    }

    /// Create a Mesh from a filePath (loads Nxmd internally) and insert into SlotMap.
    /// Dynamic: loads and uploads immediately.
    static Handle resolve(const core::NamespacedId &id,
                          std::filesystem::path filePath,
                          core::ResourceMode mode = core::ResourceMode::Dynamic);

    /// Create a builtin unit quad (2D positions + UVs) (always Dynamic).
    static Handle create(const core::NamespacedId &id);

    // --- Accessors ---

    SDL_GPUBuffer *vertexBuffer() const { return _vertexBuffer; }
    SDL_GPUBuffer *indexBuffer() const { return _indexBuffer; }
    uint32_t vertexCount() const { return _vertexCount; }
    uint32_t indexCount() const { return _indexCount; }
    SDL_GPUIndexElementSize indexType() const { return _indexType; }
    const std::vector<SDL_GPUVertexAttribute> &attributes() const { return _attributes; }
    const std::vector<SDL_GPUVertexBufferDescription> &vertexBuffers() const { return _vertexBuffers; }

    ~Mesh() override;

    Mesh(Mesh &&other) noexcept;
    Mesh &operator=(Mesh &&other) noexcept;

    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;

private:
    Mesh(const core::NamespacedId &id,
         std::filesystem::path filePath,
         core::ResourceMode mode,
         SDL_GPUBuffer *vertexBuffer,
         SDL_GPUBuffer *indexBuffer,
         uint32_t vertexCount,
         uint32_t indexCount,
         SDL_GPUIndexElementSize indexType,
         std::vector<SDL_GPUVertexAttribute> attributes,
         std::vector<SDL_GPUVertexBufferDescription> vertexBuffers);

    /// Upload vertex/index data via transfer buffer.
    static bool uploadBuffers(SDL_GPUDevice *device,
                              SDL_GPUBuffer *vertexBuffer,
                              const uint8_t *vertexData, size_t vertexDataSize,
                              SDL_GPUBuffer *indexBuffer,
                              const uint8_t *indexData, size_t indexDataSize);

    SDL_GPUBuffer *_vertexBuffer = nullptr;
    SDL_GPUBuffer *_indexBuffer = nullptr;
    uint32_t _vertexCount = 0;
    uint32_t _indexCount = 0;
    SDL_GPUIndexElementSize _indexType = SDL_GPU_INDEXELEMENTSIZE_16BIT;
    std::vector<SDL_GPUVertexAttribute> _attributes;
    std::vector<SDL_GPUVertexBufferDescription> _vertexBuffers;
};

} // namespace noix::video
