#pragma once

/*
 * GeometryDef — Runtime geometry with GPU resources.
 * Created from NxmdData, owns vertex/index GPU buffers.
 */

#include "video/NxmdData.h"

#include <SDL3/SDL_gpu.h>

#include <vector>

namespace noix::video {

class GeometryDef {
public:
    /// Create GPU resources from parsed NXMD data.
    static std::optional<GeometryDef> create(SDL_GPUDevice *device,
                                             const NxmdData &data);

    SDL_GPUBuffer *vertexBuffer() const { return _vertexBuffer; }
    SDL_GPUBuffer *indexBuffer() const { return _indexBuffer; }
    uint32_t vertexCount() const { return _vertexCount; }
    uint32_t indexCount() const { return _indexCount; }
    SDL_GPUIndexElementSize indexType() const { return _indexType; }
    const std::vector<SDL_GPUVertexAttribute> &attributes() const { return _attributes; }
    const std::vector<SDL_GPUVertexBufferDescription> &vertexBuffers() const { return _vertexBuffers; }

    void destroy(SDL_GPUDevice *device);

private:
    SDL_GPUBuffer *_vertexBuffer = nullptr;
    SDL_GPUBuffer *_indexBuffer = nullptr;
    uint32_t _vertexCount = 0;
    uint32_t _indexCount = 0;
    SDL_GPUIndexElementSize _indexType = SDL_GPU_INDEXELEMENTSIZE_16BIT;
    std::vector<SDL_GPUVertexAttribute> _attributes;
    std::vector<SDL_GPUVertexBufferDescription> _vertexBuffers;
};

} // namespace noix::video
