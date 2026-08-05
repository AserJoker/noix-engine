#pragma once

#include "core/Handle.h"
#include "core/NamespacedId.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace noix::runtime {

class AssetManager {
public:
    explicit AssetManager(std::filesystem::path basePath);
    ~AssetManager() = default;

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // --- Path resolution (existing) ---

    /// Add a resource pack directory. Later additions have higher priority.
    void addPack(const std::filesystem::path& packPath);

    /// Remove a resource pack by path. Returns false if not found.
    bool removePack(const std::filesystem::path& packPath);

    /// Move a pack one step higher in priority (toward later = higher).
    bool movePackUp(const std::filesystem::path& packPath);

    /// Move a pack one step lower in priority (toward earlier = lower).
    bool movePackDown(const std::filesystem::path& packPath);

    /// Number of added packs (excluding default).
    size_t packCount() const;

    /// List all pack paths in low→high priority order.
    std::vector<std::filesystem::path> listPacks() const;

    /// Resolve a NamespacedId to a file path, searching packs high→low priority.
    std::optional<std::filesystem::path> resolve(const core::NamespacedId& id) const;

    /// Check if a resource exists in any pack.
    bool exists(const core::NamespacedId& id) const;

    /// Get the engine default resource path (basePath).
    const std::filesystem::path& defaultPath() const { return _defaultPath; }

    // --- Generic load / unload ---

    /// Load a resource by type. Returns nullptr on failure.
    /// If already loaded, returns the existing Handle.
    template<typename T>
    core::Handle<T> *load(const core::NamespacedId &id) {
        auto it = _activeResources.find(id);
        if (it != _activeResources.end()) {
            return static_cast<core::Handle<T>*>(it->second.get());
        }

        auto path = resolve(id);
        if (!path) return nullptr;
        auto data = readFile(path->string());
        if (!data) return nullptr;

        auto handle = T::resolve(id, std::move(*data));
        if (!handle.isValid()) return nullptr;

        auto ptr = std::make_unique<core::Handle<T>>(handle);
        auto *raw = ptr.get();
        _activeResources[id] = std::move(ptr);
        return raw;
    }

    /// Unload a resource by id. Calls BaseHandle::unload() then removes from map.
    /// No-op if not loaded or is a builtin.
    void unload(const core::NamespacedId &id);

    /// Check if a resource is currently loaded.
    bool isLoaded(const core::NamespacedId &id) const;

    /// Get an already-loaded Handle without loading. Returns nullptr if not loaded.
    template<typename T>
    core::Handle<T> *find(const core::NamespacedId &id) const {
        auto it = _activeResources.find(id);
        if (it == _activeResources.end()) return nullptr;
        return static_cast<core::Handle<T>*>(it->second.get());
    }

    /// Unload all non-builtin resources.
    void unloadAll();

    // --- Builtin protection ---

    /// Mark a resource as builtin (cannot be unloaded).
    void addBuiltin(const core::NamespacedId &id);

    /// Check if a resource is a builtin.
    bool isBuiltin(const core::NamespacedId &id) const;

    // --- Builtin data ---

    /// Register in-memory data for a builtin resource.
    /// Shader::loadFromAsset uses this instead of reading from disk.
    void addBuiltinData(const core::NamespacedId &id, std::vector<uint8_t> data);

    /// Get builtin data for a resource. Returns nullptr if not registered.
    const std::vector<uint8_t> *getBuiltinData(const core::NamespacedId &id) const;

    // --- File I/O ---

    /// Write data to a NamespacedId path (always in defaultPath).
    bool write(const core::NamespacedId &id, const std::vector<uint8_t> &data);

private:
    static std::filesystem::path toRelativePath(const core::NamespacedId& id);
    std::vector<std::filesystem::path>::iterator findPack(const std::filesystem::path& packPath);
    static std::optional<std::vector<uint8_t>> readFile(const std::string &path);

    std::filesystem::path _defaultPath;
    std::vector<std::filesystem::path> _packRoots;

    std::unordered_map<core::NamespacedId, std::unique_ptr<core::BaseHandle>> _activeResources;
    std::unordered_set<core::NamespacedId> _builtins;
    std::unordered_map<core::NamespacedId, std::vector<uint8_t>> _builtinData;
};

} // namespace noix::runtime
