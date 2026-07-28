#include "resource/ResourcePack.h"
#include "core/Logger.h"
#include <algorithm>

namespace noix::resource {

ResourcePack::ResourcePack(std::filesystem::path basePath)
    : _defaultPath(std::move(basePath)) {}

void ResourcePack::addPack(const std::filesystem::path& packPath) {
    auto canonical = std::filesystem::weakly_canonical(packPath);
    _packRoots.push_back(canonical);
    core::Logger::instance().info("resource pack added: {}", packPath.string());
}

bool ResourcePack::removePack(const std::filesystem::path& packPath) {
    auto it = findPack(packPath);
    if (it == _packRoots.end()) return false;
    core::Logger::instance().info("resource pack removed: {}", it->string());
    _packRoots.erase(it);
    return true;
}

bool ResourcePack::movePackUp(const std::filesystem::path& packPath) {
    auto it = findPack(packPath);
    if (it == _packRoots.end()) return false;
    auto next = std::next(it);
    if (next == _packRoots.end()) return false; // already highest
    std::iter_swap(it, next);
    return true;
}

bool ResourcePack::movePackDown(const std::filesystem::path& packPath) {
    auto it = findPack(packPath);
    if (it == _packRoots.end()) return false;
    if (it == _packRoots.begin()) return false; // already lowest
    auto prev = std::prev(it);
    std::iter_swap(it, prev);
    return true;
}

size_t ResourcePack::packCount() const {
    return _packRoots.size();
}

std::vector<std::filesystem::path> ResourcePack::listPacks() const {
    return _packRoots;
}

std::optional<std::filesystem::path> ResourcePack::resolve(const core::NamespacedId& id) const {
    auto rel = toRelativePath(id);
    for (auto it = _packRoots.rbegin(); it != _packRoots.rend(); ++it) {
        auto candidate = *it / rel;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    auto candidate = _defaultPath / rel;
    if (std::filesystem::exists(candidate)) {
        return candidate;
    }
    return std::nullopt;
}

bool ResourcePack::exists(const core::NamespacedId& id) const {
    return resolve(id).has_value();
}

std::filesystem::path ResourcePack::toRelativePath(const core::NamespacedId& id) {
    return std::filesystem::path("resources") / id.ns() / id.name();
}

std::vector<std::filesystem::path>::iterator ResourcePack::findPack(const std::filesystem::path& packPath) {
    auto canonical = std::filesystem::weakly_canonical(packPath);
    return std::find(_packRoots.begin(), _packRoots.end(), canonical);
}

} // namespace noix::resource
