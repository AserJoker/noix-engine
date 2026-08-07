#pragma once

#include "core/Handle.h"
#include "core/NamespacedId.h"
#include "core/SlotId.h"

#include <filesystem>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace noix::runtime {

class AssetManager {
public:
    explicit AssetManager(std::filesystem::path basePath);
    ~AssetManager();

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // --- Path resolution ---

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

    // --- Generic load / create / unload ---

    /// Load a resource by type from file. Returns invalid Handle on failure.
    /// If already loaded, returns the existing Handle.
    template<typename T>
    core::Handle<T> load(const core::NamespacedId &id) {
        auto it = _activeResources.find(id);
        if (it != _activeResources.end()) {
            return core::Handle<T>(it->second.slotId);
        }

        auto path = resolve(id);
        if (!path) return {};

        auto handle = T::resolve(id, *path);
        if (!handle.isValid()) return {};

        _activeResources[id] = { handle.slotId(),
            [](core::SlotId s) { T::slotMap().remove(s); } };
        return handle;
    }

    /// Create a builtin resource by type with direct data. Returns invalid Handle on failure.
    /// If already created, returns the existing Handle.
    template<typename T, typename... Args>
    core::Handle<T> create(const core::NamespacedId &id, Args&&... args) {
        auto it = _activeResources.find(id);
        if (it != _activeResources.end()) {
            return core::Handle<T>(it->second.slotId);
        }

        auto handle = T::create(id, std::forward<Args>(args)...);
        if (!handle.isValid()) return {};

        _activeResources[id] = { handle.slotId(),
            [](core::SlotId s) { T::slotMap().remove(s); } };
        return handle;
    }

    /// Unload a resource by id. Removes from SlotMap then removes from map.
    /// No-op if not loaded or is a builtin.
    void unload(const core::NamespacedId &id);

    /// Check if a resource is currently loaded.
    bool isLoaded(const core::NamespacedId &id) const;

    /// Get an already-loaded Handle without loading. Returns invalid Handle if not loaded.
    template<typename T>
    core::Handle<T> find(const core::NamespacedId &id) const {
        auto it = _activeResources.find(id);
        if (it == _activeResources.end()) return {};
        return core::Handle<T>(it->second.slotId);
    }

    /// Unload all non-builtin resources.
    void unloadAll();

    /// Unload ALL resources including builtins. Called before GPU device destruction.
    void shutdown();

    // --- Builtin protection ---

    /// Mark a resource as builtin (cannot be unloaded).
    void addBuiltin(const core::NamespacedId &id);

    /// Check if a resource is a builtin.
    bool isBuiltin(const core::NamespacedId &id) const;

    // --- Builtin data ---

    /// Register in-memory data for a builtin resource.
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

    /// Type-erased unload function for a stored SlotId.
    using UnloadFn = void(*)(core::SlotId);

    struct ResourceEntry {
        core::SlotId slotId{};
        UnloadFn unload = nullptr;
    };

    std::filesystem::path _defaultPath;
    std::vector<std::filesystem::path> _packRoots;

    std::unordered_map<core::NamespacedId, ResourceEntry> _activeResources;
    std::unordered_set<core::NamespacedId> _builtins;
    std::unordered_map<core::NamespacedId, std::vector<uint8_t>> _builtinData;
};

} // namespace noix::runtime
