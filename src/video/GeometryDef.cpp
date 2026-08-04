#include "video/GeometryDef.h"
#include "core/Logger.h"

#include <SDL3/SDL.h>

#include <cstring>

namespace noix::video {

std::optional<GeometryDef> GeometryDef::create(SDL_GPUDevice *device,
                                                const NxmdData &data) {
    GeometryDef geom;

    // --- Create and upload vertex buffer ---
    if (data.vertexDataSize() == 0) {
        core::Logger::instance().error("GeometryDef: No vertex data");
        return std::nullopt;
    }

    SDL_GPUBufferCreateInfo bufInfo{};
    bufInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bufInfo.size = static_cast<Uint32>(data.vertexDataSize());
    bufInfo.props = 0;
    geom._vertexBuffer = SDL_CreateGPUBuffer(device, &bufInfo);
    if (!geom._vertexBuffer) {
        core::Logger::instance().error("GeometryDef: Failed to create vertex buffer: {}",
                                       SDL_GetError());
        return std::nullopt;
    }

    // Upload via transfer buffer
    SDL_GPUTransferBufferCreateInfo xferInfo{};
    xferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    xferInfo.size = static_cast<Uint32>(data.vertexDataSize());
    xferInfo.props = 0;
    SDL_GPUTransferBuffer *xferBuf = SDL_CreateGPUTransferBuffer(device, &xferInfo);
    if (!xferBuf) {
        core::Logger::instance().error("GeometryDef: Failed to create transfer buffer");
        geom.destroy(device);
        return std::nullopt;
    }

    void *mapped = SDL_MapGPUTransferBuffer(device, xferBuf, false);
    SDL_memcpy(mapped, data.vertexData(), data.vertexDataSize());
    SDL_UnmapGPUTransferBuffer(device, xferBuf);

    SDL_GPUCommandBuffer *uploadCmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(uploadCmd);

    SDL_GPUTransferBufferLocation src{};
    src.offset = 0;
    src.transfer_buffer = xferBuf;

    SDL_GPUBufferRegion dst{};
    dst.buffer = geom._vertexBuffer;
    dst.offset = 0;
    dst.size = static_cast<Uint32>(data.vertexDataSize());

    SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);
    SDL_EndGPUCopyPass(copyPass);

    // --- Create and upload index buffer ---
    if (data.indexDataSize() > 0) {
        SDL_GPUBufferCreateInfo ibInfo{};
        ibInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        ibInfo.size = static_cast<Uint32>(data.indexDataSize());
        ibInfo.props = 0;
        geom._indexBuffer = SDL_CreateGPUBuffer(device, &ibInfo);
        if (!geom._indexBuffer) {
            core::Logger::instance().error("GeometryDef: Failed to create index buffer");
            SDL_SubmitGPUCommandBuffer(uploadCmd);
            SDL_ReleaseGPUTransferBuffer(device, xferBuf);
            geom.destroy(device);
            return std::nullopt;
        }

        SDL_GPUTransferBufferCreateInfo ixferInfo{};
        ixferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ixferInfo.size = static_cast<Uint32>(data.indexDataSize());
        ixferInfo.props = 0;
        SDL_GPUTransferBuffer *ixferBuf = SDL_CreateGPUTransferBuffer(device, &ixferInfo);
        if (!ixferBuf) {
            SDL_SubmitGPUCommandBuffer(uploadCmd);
            SDL_ReleaseGPUTransferBuffer(device, xferBuf);
            geom.destroy(device);
            return std::nullopt;
        }

        void *imapped = SDL_MapGPUTransferBuffer(device, ixferBuf, false);
        SDL_memcpy(imapped, data.indexData(), data.indexDataSize());
        SDL_UnmapGPUTransferBuffer(device, ixferBuf);

        SDL_GPUCopyPass *iCopyPass = SDL_BeginGPUCopyPass(uploadCmd);
        SDL_GPUTransferBufferLocation isrc{};
        isrc.offset = 0;
        isrc.transfer_buffer = ixferBuf;

        SDL_GPUBufferRegion idst{};
        idst.buffer = geom._indexBuffer;
        idst.offset = 0;
        idst.size = static_cast<Uint32>(data.indexDataSize());

        SDL_UploadToGPUBuffer(iCopyPass, &isrc, &idst, false);
        SDL_EndGPUCopyPass(iCopyPass);

        SDL_SubmitGPUCommandBuffer(uploadCmd);
        SDL_ReleaseGPUTransferBuffer(device, xferBuf);
        SDL_ReleaseGPUTransferBuffer(device, ixferBuf);
    } else {
        SDL_SubmitGPUCommandBuffer(uploadCmd);
        SDL_ReleaseGPUTransferBuffer(device, xferBuf);
    }

    // Copy metadata
    geom._vertexCount = data.vertexCount();
    geom._indexCount = data.indexCount();
    geom._indexType =
        data.indexType() == INDEX_UINT16
            ? SDL_GPU_INDEXELEMENTSIZE_16BIT
            : SDL_GPU_INDEXELEMENTSIZE_32BIT;

    // Build vertex layout from attributes
    uint32_t stride = 0;
    if (!data.attributes().empty()) {
        static const uint32_t formatSizes[] = {8, 12, 16, 2, 4, 2, 4, 4, 8, 4, 8};
        const auto &last = data.attributes().back();
        stride = last.offset + formatSizes[last.format];
    }

    SDL_GPUVertexBufferDescription vbDesc{};
    vbDesc.slot = 0;
    vbDesc.pitch = stride;
    vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vbDesc.instance_step_rate = 0;
    geom._vertexBuffers.push_back(vbDesc);

    // Map NXMD VertexFormat → SDL_GPUVertexElementFormat
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

    geom._attributes.clear();
    for (const auto &attr : data.attributes()) {
        SDL_GPUVertexAttribute gpuAttr{};
        gpuAttr.location = attr.location;
        gpuAttr.buffer_slot = 0;
        gpuAttr.format = vfToSDL[attr.format];
        gpuAttr.offset = attr.offset;
        geom._attributes.push_back(gpuAttr);
    }

    return geom;
}

void GeometryDef::destroy(SDL_GPUDevice *device) {
    if (_vertexBuffer) {
        SDL_ReleaseGPUBuffer(device, _vertexBuffer);
        _vertexBuffer = nullptr;
    }
    if (_indexBuffer) {
        SDL_ReleaseGPUBuffer(device, _indexBuffer);
        _indexBuffer = nullptr;
    }
    _vertexCount = 0;
    _indexCount = 0;
}

} // namespace noix::video
