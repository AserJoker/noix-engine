#include "video/MaterialCache.h"
#include "video/MaterialDef.h"
#include "core/Logger.h"
#include "runtime/AssetManager.h"

namespace noix::video {

ResourceHandle MaterialCache::create(const core::NamespacedId &id,
                                     runtime::AssetManager &assetMgr) {
    if (auto existing = findById(id); existing.has_value()) {
        return *existing;
    }

    auto matPath = assetMgr.resolve(id);
    if (!matPath.has_value()) {
        core::Logger::instance().error(
            "MaterialCache: Material not found: {}", id.toString());
        return {};
    }

    auto material = MaterialDef::load(matPath->string());
    if (!material.has_value()) {
        core::Logger::instance().error(
            "MaterialCache: Failed to parse material: {}", id.toString());
        return {};
    }

    ResourceHandle handle;
    if (!_freeList.empty()) {
        uint32_t idx = _freeList.back();
        _freeList.pop_back();
        _slots[idx].material = std::move(*material);
        _slots[idx].generation++;
        handle = {idx, _slots[idx].generation};
    } else {
        handle = {static_cast<uint32_t>(_slots.size()), 0};
        _slots.push_back({std::move(*material), 0});
    }
    _idToHandle[id] = handle;
    return handle;
}

void MaterialCache::destroy(ResourceHandle handle) {
    if (!handle.isValid() || handle.index >= _slots.size()) return;
    auto &slot = _slots[handle.index];
    if (slot.generation != handle.generation) return;

    slot.generation++;
    _freeList.push_back(handle.index);

    for (auto it = _idToHandle.begin(); it != _idToHandle.end(); ++it) {
        if (it->second == handle) {
            _idToHandle.erase(it);
            break;
        }
    }
}

MaterialDef *MaterialCache::get(ResourceHandle handle) const {
    if (!handle.isValid() || handle.index >= _slots.size()) return nullptr;
    const auto &slot = _slots[handle.index];
    if (slot.generation != handle.generation) return nullptr;
    return const_cast<MaterialDef *>(&slot.material);
}

std::optional<ResourceHandle>
MaterialCache::findById(const core::NamespacedId &id) const {
    auto it = _idToHandle.find(id);
    if (it == _idToHandle.end()) return std::nullopt;
    return it->second;
}

void MaterialCache::addBuiltin(const core::NamespacedId &id) {
    _builtins.insert(id);
}

void MaterialCache::update(const std::vector<core::NamespacedId> &targetIds,
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
        destroy(handle);
    }

    for (auto &id : toAdd) {
        create(id, assetMgr);
    }
}

} // namespace noix::video
