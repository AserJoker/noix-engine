#include "video/Mesh.h"
#include "core/Logger.h"
#include "runtime/Application.h"
#include "video/Renderer.h"

#include <SDL3/SDL.h>
#include <cstring>

namespace noix::video {

// ---- NXMD VertexFormat → SDL_GPUVertexElementFormat ----

static const SDL_GPUVertexElementFormat vfToSDL[] = {
    SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,       // VF_FLOAT2
    SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,       // VF_FLOAT3
    SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,       // VF_FLOAT4
    SDL_GPU_VERTEXELEMENTFORMAT_BYTE2_NORM,   // VF_BYTE2_NORM
    SDL_GPU_VERTEXELEMENTFORMAT_BYTE4_NORM,   // VF_BYTE4_NORM
    SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2_NORM,  // VF_UBYTE2_NORM
    SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,  // VF_UBYTE4_NORM
    SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM,  // VF_SHORT2_NORM
    SDL_GPU_VERTEXELEMENTFORMAT_SHORT4_NORM,  // VF_SHORT4_NORM
    SDL_GPU_VERTEXELEMENTFORMAT_USHORT2_NORM, // VF_USHORT2_NORM
    SDL_GPU_VERTEXELEMENTFORMAT_USHORT4_NORM, // VF_USHORT4_NORM
};

static const uint32_t formatSizes[] = {8, 12, 16, 2, 4, 2, 4, 4, 8, 4, 8};

// ---- Mesh Resource ----

Mesh::Mesh(const core::NamespacedId &id,
           std::filesystem::path filePath,
           core::ResourceMode mode,
           SDL_GPUBuffer *vertexBuffer,
           SDL_GPUBuffer *indexBuffer,
           uint32_t vertexCount,
           uint32_t indexCount,
           SDL_GPUIndexElementSize indexType,
           std::vector<SDL_GPUVertexAttribute> attributes,
           std::vector<SDL_GPUVertexBufferDescription> vertexBuffers)
    : core::Resource(id, std::move(filePath), mode),
      _vertexBuffer(vertexBuffer),
      _indexBuffer(indexBuffer),
      _vertexCount(vertexCount),
      _indexCount(indexCount),
      _indexType(indexType),
      _attributes(std::move(attributes)),
      _vertexBuffers(std::move(vertexBuffers)) {}

Mesh::~Mesh() {
    if (_vertexBuffer || _indexBuffer) {
        auto *device = runtime::Application::instance()
                           .renderer().gpuDevice();
        if (device) {
            if (_vertexBuffer) SDL_ReleaseGPUBuffer(device, _vertexBuffer);
            if (_indexBuffer) SDL_ReleaseGPUBuffer(device, _indexBuffer);
        }
        _vertexBuffer = nullptr;
        _indexBuffer = nullptr;
    }
}

Mesh::Mesh(Mesh &&other) noexcept
    : core::Resource(std::move(other)),
      _vertexBuffer(other._vertexBuffer),
      _indexBuffer(other._indexBuffer),
      _vertexCount(other._vertexCount),
      _indexCount(other._indexCount),
      _indexType(other._indexType),
      _attributes(std::move(other._attributes)),
      _vertexBuffers(std::move(other._vertexBuffers)) {
    other._vertexBuffer = nullptr;
    other._indexBuffer = nullptr;
    other._vertexCount = 0;
    other._indexCount = 0;
}

Mesh &Mesh::operator=(Mesh &&other) noexcept {
    if (this != &other) {
        if (_vertexBuffer || _indexBuffer) {
            auto *device = runtime::Application::instance()
                               .renderer().gpuDevice();
            if (device) {
                if (_vertexBuffer) SDL_ReleaseGPUBuffer(device, _vertexBuffer);
                if (_indexBuffer) SDL_ReleaseGPUBuffer(device, _indexBuffer);
            }
        }
        core::Resource::operator=(std::move(other));
        _vertexBuffer = other._vertexBuffer;
        _indexBuffer = other._indexBuffer;
        _vertexCount = other._vertexCount;
        _indexCount = other._indexCount;
        _indexType = other._indexType;
        _attributes = std::move(other._attributes);
        _vertexBuffers = std::move(other._vertexBuffers);
        other._vertexBuffer = nullptr;
        other._indexBuffer = nullptr;
        other._vertexCount = 0;
        other._indexCount = 0;
    }
    return *this;
}

// ---- GPU upload helper ----

bool Mesh::uploadBuffers(SDL_GPUDevice *device,
                          SDL_GPUBuffer *vertexBuffer,
                          const uint8_t *vertexData, size_t vertexDataSize,
                          SDL_GPUBuffer *indexBuffer,
                          const uint8_t *indexData, size_t indexDataSize) {
    // Calculate total transfer size
    size_t totalSize = vertexDataSize + indexDataSize;
    SDL_GPUTransferBufferCreateInfo xferInfo{};
    xferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    xferInfo.size = static_cast<Uint32>(totalSize);
    xferInfo.props = 0;
    SDL_GPUTransferBuffer *xferBuf = SDL_CreateGPUTransferBuffer(device, &xferInfo);
    if (!xferBuf) {
        core::Logger::instance().error("Mesh: Failed to create transfer buffer");
        return false;
    }

    void *mapped = SDL_MapGPUTransferBuffer(device, xferBuf, false);
    if (!mapped) {
        core::Logger::instance().error("Mesh: Failed to map transfer buffer");
        SDL_ReleaseGPUTransferBuffer(device, xferBuf);
        return false;
    }
    SDL_memcpy(mapped, vertexData, vertexDataSize);
    if (indexData && indexDataSize > 0) {
        SDL_memcpy(static_cast<uint8_t *>(mapped) + vertexDataSize,
                   indexData, indexDataSize);
    }
    SDL_UnmapGPUTransferBuffer(device, xferBuf);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);

    // Upload vertex buffer
    SDL_GPUCopyPass *vCopy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation vSrc{};
    vSrc.offset = 0;
    vSrc.transfer_buffer = xferBuf;
    SDL_GPUBufferRegion vDst{};
    vDst.buffer = vertexBuffer;
    vDst.offset = 0;
    vDst.size = static_cast<Uint32>(vertexDataSize);
    SDL_UploadToGPUBuffer(vCopy, &vSrc, &vDst, false);
    SDL_EndGPUCopyPass(vCopy);

    // Upload index buffer
    if (indexBuffer && indexDataSize > 0) {
        SDL_GPUCopyPass *iCopy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation iSrc{};
        iSrc.offset = static_cast<Uint32>(vertexDataSize);
        iSrc.transfer_buffer = xferBuf;
        SDL_GPUBufferRegion iDst{};
        iDst.buffer = indexBuffer;
        iDst.offset = 0;
        iDst.size = static_cast<Uint32>(indexDataSize);
        SDL_UploadToGPUBuffer(iCopy, &iSrc, &iDst, false);
        SDL_EndGPUCopyPass(iCopy);
    }

    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device, xferBuf);
    return true;
}

// ---- resolve: from filePath ----

Mesh::Handle Mesh::resolve(const core::NamespacedId &id,
                            std::filesystem::path filePath,
                            core::ResourceMode mode) {
    // Load Nxmd from file
    auto nxmdHandle = Nxmd::resolve(id, filePath, mode);
    if (!nxmdHandle.isValid()) return {};

    Nxmd *nxmd = nxmdHandle.get();
    NxmdPayloadRef payloadRef = nxmd->data();
    if (!payloadRef || payloadRef->vertexData.empty()) {
        core::Logger::instance().error("Mesh: No vertex data in Nxmd: {}", id.toString());
        return {};
    }

    auto *device = runtime::Application::instance()
                       .renderer().gpuDevice();
    if (!device) return {};

    // Create vertex buffer
    SDL_GPUBufferCreateInfo vbInfo{};
    vbInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    vbInfo.size = static_cast<Uint32>(payloadRef->vertexData.size());
    vbInfo.props = 0;
    SDL_GPUBuffer *vertexBuffer = SDL_CreateGPUBuffer(device, &vbInfo);
    if (!vertexBuffer) {
        core::Logger::instance().error("Mesh: Failed to create vertex buffer: {}", SDL_GetError());
        return {};
    }

    // Create index buffer (optional)
    SDL_GPUBuffer *indexBuffer = nullptr;
    if (!payloadRef->indexData.empty()) {
        SDL_GPUBufferCreateInfo ibInfo{};
        ibInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        ibInfo.size = static_cast<Uint32>(payloadRef->indexData.size());
        ibInfo.props = 0;
        indexBuffer = SDL_CreateGPUBuffer(device, &ibInfo);
        if (!indexBuffer) {
            core::Logger::instance().error("Mesh: Failed to create index buffer");
            SDL_ReleaseGPUBuffer(device, vertexBuffer);
            return {};
        }
    }

    // Upload data
    if (!uploadBuffers(device, vertexBuffer,
                       payloadRef->vertexData.data(), payloadRef->vertexData.size(),
                       indexBuffer,
                       indexBuffer ? payloadRef->indexData.data() : nullptr,
                       indexBuffer ? payloadRef->indexData.size() : 0)) {
        if (indexBuffer) SDL_ReleaseGPUBuffer(device, indexBuffer);
        SDL_ReleaseGPUBuffer(device, vertexBuffer);
        return {};
    }

    // Build vertex layout from attributes
    uint32_t stride = 0;
    if (!payloadRef->attributes.empty()) {
        const auto &last = payloadRef->attributes.back();
        stride = last.offset + formatSizes[last.format];
    }

    std::vector<SDL_GPUVertexBufferDescription> vertexBuffers;
    SDL_GPUVertexBufferDescription vbDesc{};
    vbDesc.slot = 0;
    vbDesc.pitch = stride;
    vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vbDesc.instance_step_rate = 0;
    vertexBuffers.push_back(vbDesc);

    std::vector<SDL_GPUVertexAttribute> attributes;
    for (const auto &attr : payloadRef->attributes) {
        SDL_GPUVertexAttribute gpuAttr{};
        gpuAttr.location = attr.location;
        gpuAttr.buffer_slot = 0;
        gpuAttr.format = vfToSDL[attr.format];
        gpuAttr.offset = attr.offset;
        attributes.push_back(gpuAttr);
    }

    SDL_GPUIndexElementSize indexType =
        payloadRef->indexType == INDEX_UINT16
            ? SDL_GPU_INDEXELEMENTSIZE_16BIT
            : SDL_GPU_INDEXELEMENTSIZE_32BIT;

    Mesh mesh(id, std::move(filePath), mode,
              vertexBuffer, indexBuffer,
              payloadRef->vertexCount, payloadRef->indexCount, indexType,
              std::move(attributes), std::move(vertexBuffers));
    return Handle(slotMap().insert(std::move(mesh)));
}

// ---- createBuiltinQuad ----

Mesh::Handle Mesh::createBuiltinQuad(const core::NamespacedId &id) {
    auto *device = runtime::Application::instance()
                       .renderer().gpuDevice();
    if (!device) return {};

    // 4 vertices: position (float2) + texcoord (float2), stride = 16 bytes
    struct Vertex { float x, y, u, v; };
    Vertex verts[4] = {
        {-0.5f, -0.5f, 0.0f, 0.0f}, // v0: top-left
        { 0.5f, -0.5f, 1.0f, 0.0f}, // v1: top-right
        {-0.5f,  0.5f, 0.0f, 1.0f}, // v2: bottom-left
        { 0.5f,  0.5f, 1.0f, 1.0f}, // v3: bottom-right
    };
    // Two CCW triangles in screen space:
    // tri0: v0→v2→v3 (top-left → bottom-left → bottom-right)
    // tri1: v0→v3→v1 (top-left → bottom-right → top-right)
    uint16_t indices[6] = {0, 2, 3, 0, 3, 1};

    // Create vertex buffer
    SDL_GPUBufferCreateInfo vbInfo{};
    vbInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    vbInfo.size = sizeof(verts);
    vbInfo.props = 0;
    SDL_GPUBuffer *vertexBuffer = SDL_CreateGPUBuffer(device, &vbInfo);
    if (!vertexBuffer) return {};

    // Create index buffer
    SDL_GPUBufferCreateInfo ibInfo{};
    ibInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    ibInfo.size = sizeof(indices);
    ibInfo.props = 0;
    SDL_GPUBuffer *indexBuffer = SDL_CreateGPUBuffer(device, &ibInfo);
    if (!indexBuffer) {
        SDL_ReleaseGPUBuffer(device, vertexBuffer);
        return {};
    }

    // Upload
    if (!uploadBuffers(device, vertexBuffer,
                       reinterpret_cast<const uint8_t *>(verts), sizeof(verts),
                       indexBuffer,
                       reinterpret_cast<const uint8_t *>(indices), sizeof(indices))) {
        SDL_ReleaseGPUBuffer(device, indexBuffer);
        SDL_ReleaseGPUBuffer(device, vertexBuffer);
        return {};
    }

    // Vertex layout
    std::vector<SDL_GPUVertexBufferDescription> vertexBuffers;
    SDL_GPUVertexBufferDescription vbDesc{};
    vbDesc.slot = 0;
    vbDesc.pitch = sizeof(Vertex);
    vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vbDesc.instance_step_rate = 0;
    vertexBuffers.push_back(vbDesc);

    std::vector<SDL_GPUVertexAttribute> attributes;
    attributes.push_back({0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0});
    attributes.push_back({1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 8});

    Mesh mesh(id, "", core::ResourceMode::Dynamic,
              vertexBuffer, indexBuffer,
              4, 6, SDL_GPU_INDEXELEMENTSIZE_16BIT,
              std::move(attributes), std::move(vertexBuffers));
    return Handle(slotMap().insert(std::move(mesh)));
}

} // namespace noix::video
