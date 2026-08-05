#include "runtime/AssetManager.h"
#include "core/Logger.h"

#include <algorithm>
#include <fstream>

#ifdef _WIN32
#define CRT_SECURE_NO_WARNINGS
#endif

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
    if (next == _packRoots.end()) return false;
    std::iter_swap(it, next);
    return true;
}

bool AssetManager::movePackDown(const std::filesystem::path& packPath) {
    auto it = findPack(packPath);
    if (it == _packRoots.end()) return false;
    if (it == _packRoots.begin()) return false;
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
    for (auto it = _packRoots.rbegin(); it != _packRoots.rend(); ++it) {
        auto candidate = *it / rel;
        if (std::filesystem::exists(candidate)) return candidate;
    }
    auto candidate = _defaultPath / rel;
    if (std::filesystem::exists(candidate)) return candidate;
    return std::nullopt;
}

bool AssetManager::exists(const core::NamespacedId& id) const {
    return resolve(id).has_value();
}

// --- Generic load / unload ---

void AssetManager::unload(const core::NamespacedId &id) {
    if (_builtins.count(id)) return;
    auto it = _activeResources.find(id);
    if (it == _activeResources.end()) return;
    it->second->unload();
    _activeResources.erase(it);
}

bool AssetManager::isLoaded(const core::NamespacedId &id) const {
    return _activeResources.count(id) > 0;
}

void AssetManager::unloadAll() {
    for (auto it = _activeResources.begin(); it != _activeResources.end(); ) {
        if (_builtins.count(it->first)) {
            ++it;
        } else {
            it->second->unload();
            it = _activeResources.erase(it);
        }
    }
}

// --- Builtin protection ---

void AssetManager::addBuiltin(const core::NamespacedId &id) {
    _builtins.insert(id);
}

bool AssetManager::isBuiltin(const core::NamespacedId &id) const {
    return _builtins.count(id) > 0;
}

void AssetManager::addBuiltinData(const core::NamespacedId &id, std::vector<uint8_t> data) {
    _builtinData[id] = std::move(data);
    _builtins.insert(id);
}

const std::vector<uint8_t> *AssetManager::getBuiltinData(const core::NamespacedId &id) const {
    auto it = _builtinData.find(id);
    return it != _builtinData.end() ? &it->second : nullptr;
}

// --- File I/O ---

bool AssetManager::write(const core::NamespacedId &id, const std::vector<uint8_t> &data) {
    auto rel = toRelativePath(id);
    auto fullPath = _defaultPath / rel;

    // Create parent directories
    auto parent = fullPath.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent)) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream file(fullPath, std::ios::binary);
    if (!file.is_open()) {
        core::Logger::instance().error("AssetManager: Failed to write: {}",
                                       fullPath.string());
        return false;
    }
    file.write(reinterpret_cast<const char *>(data.data()),
               static_cast<std::streamsize>(data.size()));
    return true;
}

std::optional<std::vector<uint8_t>> AssetManager::readFile(const std::string &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return std::nullopt;

    auto size = file.tellg();
    if (size < 0) return std::nullopt;
    file.seekg(0);

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    file.read(reinterpret_cast<char *>(buffer.data()), size);
    if (!file) return std::nullopt;

    return buffer;
}

std::filesystem::path AssetManager::toRelativePath(const core::NamespacedId& id) {
    return std::filesystem::path("assets") / id.ns() / id.name();
}

std::vector<std::filesystem::path>::iterator AssetManager::findPack(const std::filesystem::path& packPath) {
    auto canonical = std::filesystem::weakly_canonical(packPath);
    return std::find(_packRoots.begin(), _packRoots.end(), canonical);
}

} // namespace noix::runtime
