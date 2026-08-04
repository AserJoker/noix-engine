#include "video/MeshCache.h"
#include "video/GeometryDef.h"
#include "video/NxmdData.h"
#include "core/Logger.h"
#include "runtime/AssetManager.h"

namespace noix::video {

ResourceHandle MeshCache::create(const core::NamespacedId &id,
                                  SDL_GPUDevice *device,
                                  runtime::AssetManager &assetMgr) {
    if (auto existing = findById(id); existing.has_value()) {
        return *existing;
    }

    std::optional<GeometryDef> geom;

    // Builtin quad: code-generated
    if (id == core::NamespacedId("noix", "builtin-quad")) {
        geom = GeometryDef::createBuiltinQuad(device);
    } else {
        auto geomPath = assetMgr.resolve(id);
        if (!geomPath.has_value()) {
            core::Logger::instance().error(
                "MeshCache: Mesh not found: {}", id.toString());
            return {};
        }
        auto nxmdData = NxmdData::load(geomPath->string());
        if (!nxmdData.has_value()) {
            core::Logger::instance().error(
                "MeshCache: Failed to parse mesh: {}", id.toString());
            return {};
        }
        geom = GeometryDef::create(device, *nxmdData);
    }

    if (!geom.has_value()) {
        core::Logger::instance().error(
            "MeshCache: Failed to create geometry: {}", id.toString());
        return {};
    }

    ResourceHandle handle;
    if (!_freeList.empty()) {
        uint32_t idx = _freeList.back();
        _freeList.pop_back();
        _slots[idx].geometry = std::move(*geom);
        _slots[idx].generation++;
        handle = {idx, _slots[idx].generation};
    } else {
        handle = {static_cast<uint32_t>(_slots.size()), 0};
        _slots.push_back({std::move(*geom), 0});
    }
    _idToHandle[id] = handle;
    return handle;
}

void MeshCache::destroy(ResourceHandle handle, SDL_GPUDevice *device) {
    if (!handle.isValid() || handle.index >= _slots.size()) return;
    auto &slot = _slots[handle.index];
    if (slot.generation != handle.generation) return;

    slot.geometry.destroy(device);
    slot.generation++;
    _freeList.push_back(handle.index);

    for (auto it = _idToHandle.begin(); it != _idToHandle.end(); ++it) {
        if (it->second == handle) {
            _idToHandle.erase(it);
            break;
        }
    }
}

GeometryDef *MeshCache::get(ResourceHandle handle) const {
    if (!handle.isValid() || handle.index >= _slots.size()) return nullptr;
    const auto &slot = _slots[handle.index];
    if (slot.generation != handle.generation) return nullptr;
    return const_cast<GeometryDef *>(&slot.geometry);
}

std::optional<ResourceHandle>
MeshCache::findById(const core::NamespacedId &id) const {
    auto it = _idToHandle.find(id);
    if (it == _idToHandle.end()) return std::nullopt;
    return it->second;
}

void MeshCache::addBuiltin(const core::NamespacedId &id) {
    _builtins.insert(id);
}

void MeshCache::update(const std::vector<core::NamespacedId> &targetIds,
                        SDL_GPUDevice *device,
                        runtime::AssetManager &assetMgr) {
    std::vector<core::NamespacedId> toRemove;
    for (auto &[id, handle] : _idToHandle) {
        if (_builtins.count(id)) continue;
        bool inTarget = false;
        for (auto &tid : targetIds) {
            if (tid == id) { inTarget = true; break; }
        }
        if (!inTarget) toRemove.push_back(id);
    }

    std::vector<core::NamespacedId> toAdd;
    for (auto &tid : targetIds) {
        if (_idToHandle.find(tid) == _idToHandle.end()) {
            toAdd.push_back(tid);
        }
    }

    for (auto &id : toRemove) {
        auto handle = _idToHandle[id];
        destroy(handle, device);
    }

    for (auto &id : toAdd) {
        create(id, device, assetMgr);
    }
}

} // namespace noix::video
