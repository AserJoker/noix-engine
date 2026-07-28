#pragma once

#include "core/NamespacedId.h"
#include <filesystem>
#include <optional>
#include <vector>

namespace noix::resource {

class ResourcePack {
public:
    explicit ResourcePack(std::filesystem::path basePath);
    ~ResourcePack() = default;

    ResourcePack(const ResourcePack&) = delete;
    ResourcePack& operator=(const ResourcePack&) = delete;

    /// Add a resource pack directory. Later additions have higher priority.
    void addPack(const std::filesystem::path& packPath);

    /// Remove a resource pack by path. Returns false if not found.
    bool removePack(const std::filesystem::path& packPath);

    /// Move a pack one step higher in priority (toward later = higher).
    /// Returns false if not found or already at highest priority.
    bool movePackUp(const std::filesystem::path& packPath);

    /// Move a pack one step lower in priority (toward earlier = lower).
    /// Returns false if not found or already at lowest priority.
    bool movePackDown(const std::filesystem::path& packPath);

    /// Number of added packs (excluding default).
    size_t packCount() const;

    /// List all pack paths in low→high priority order.
    std::vector<std::filesystem::path> listPacks() const;

    /// Resolve a NamespacedId to a file path, searching packs high→low priority.
    /// Returns nullopt if not found in any pack.
    std::optional<std::filesystem::path> resolve(const core::NamespacedId& id) const;

    /// Check if a resource exists in any pack.
    bool exists(const core::NamespacedId& id) const;

    /// Get the engine default resource path (basePath).
    const std::filesystem::path& defaultPath() const { return _defaultPath; }

private:
    static std::filesystem::path toRelativePath(const core::NamespacedId& id);
    std::vector<std::filesystem::path>::iterator findPack(const std::filesystem::path& packPath);

    std::filesystem::path _defaultPath;
    std::vector<std::filesystem::path> _packRoots;
};

} // namespace noix::resource
