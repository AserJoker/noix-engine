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

    /// Resolve a NamespacedId to a file path, searching packs high→low priority.
    /// Returns nullopt if not found in any pack.
    std::optional<std::filesystem::path> resolve(const core::NamespacedId& id) const;

    /// Check if a resource exists in any pack.
    bool exists(const core::NamespacedId& id) const;

    /// Get the engine default resource path (basePath).
    const std::filesystem::path& defaultPath() const { return _defaultPath; }

private:
    static std::filesystem::path toRelativePath(const core::NamespacedId& id);

    std::filesystem::path _defaultPath;
    std::vector<std::filesystem::path> _packRoots;
};

} // namespace noix::resource
