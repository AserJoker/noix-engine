#include "runtime/AssetManager.h"
#include "core/Logger.h"
#include <algorithm>

namespace noix::runtime {

AssetManager::AssetManager(std::filesystem::path basePath)
    : _defaultPath(std::move(basePath)) {}

void AssetManager::addPack(const std::filesystem::path& packPath) {
    auto canonical = std::filesystem::weakly_canonical(packPath);
    _packRoots.push_back(canonical);
    core::Logger::instance().info("resource pack added: {}", packPath.string());
}

bool AssetManager::removePack(const std::filesystem::path& packPath) {
    auto it = findPack(packPath);
    if (it == _packRoots.end()) return false;
    core::Logger::instance().info("resource pack removed: {}", it->string());
    _packRoots.erase(it);
    return true;
}

bool AssetManager::movePackUp(const std::filesystem::path& packPath) {
    auto it = findPack(packPath);
    if (it == _packRoots.end()) return false;
    auto next = std::next(it);
    if (next == _packRoots.end()) return false; // already highest
    std::iter_swap(it, next);
    return true;
}

bool AssetManager::movePackDown(const std::filesystem::path& packPath) {
    auto it = findPack(packPath);
    if (it == _packRoots.end()) return false;
    if (it == _packRoots.begin()) return false; // already lowest
    auto prev = std::prev(it);
    std::iter_swap(it, prev);
    return true;
}

size_t AssetManager::packCount() const {
    return _packRoots.size();
}

std::vector<std::filesystem::path> AssetManager::listPacks() const {
    return _packRoots;
}

std::optional<std::filesystem::path> AssetManager::resolve(const core::NamespacedId& id) const {
    auto rel = toRelativePath(id);

    // Try exact path first, then with common extensions
    static const char *extensions[] = {".json", ".nxmd"};
    for (auto it = _packRoots.rbegin(); it != _packRoots.rend(); ++it) {
        auto candidate = *it / rel;
        if (std::filesystem::exists(candidate)) return candidate;
        for (const char *ext : extensions) {
            auto withExt = candidate;
            withExt += ext;
            if (std::filesystem::exists(withExt)) return withExt;
        }
    }
    auto candidate = _defaultPath / rel;
    if (std::filesystem::exists(candidate)) return candidate;
    for (const char *ext : extensions) {
        auto withExt = candidate;
        withExt += ext;
        if (std::filesystem::exists(withExt)) return withExt;
    }
    return std::nullopt;
}

bool AssetManager::exists(const core::NamespacedId& id) const {
    return resolve(id).has_value();
}

std::filesystem::path AssetManager::toRelativePath(const core::NamespacedId& id) {
    return std::filesystem::path("assets") / id.ns() / id.name();
}

std::vector<std::filesystem::path>::iterator AssetManager::findPack(const std::filesystem::path& packPath) {
    auto canonical = std::filesystem::weakly_canonical(packPath);
    return std::find(_packRoots.begin(), _packRoots.end(), canonical);
}

} // namespace noix::runtime
