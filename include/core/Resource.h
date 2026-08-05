#pragma once

/*
 * Resource — Base class for all CPU-side resources.
 *
 * Two modes determined at creation time:
 * - Dynamic: owns data in memory, editable at runtime, freed on destruction.
 * - Static:  holds only the file path, re-reads from disk on each access,
 *            no memory overhead for content, not editable.
 */

#include "core/NamespacedId.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace noix::core {

enum class ResourceMode : uint8_t {
    Dynamic,  // data held in memory, editable
    Static,   // only path held, re-read on access, read-only
};

class Resource {
public:
    virtual ~Resource() = default;

    ResourceMode mode() const { return _mode; }
    const NamespacedId &id() const { return _id; }
    const std::filesystem::path &filePath() const { return _filePath; }

    /// Whether this resource is editable. True only for Dynamic mode.
    bool isEditable() const { return _mode == ResourceMode::Dynamic; }

protected:
    Resource(NamespacedId id, std::filesystem::path filePath, ResourceMode mode)
        : _id(std::move(id)), _filePath(std::move(filePath)), _mode(mode) {}

    /// Read file content from disk. Used by Static mode to re-read on access.
    std::vector<uint8_t> readFileContent() const;

    /// Assert that the resource is editable (Dynamic mode).
    /// Call in mutator methods to enforce read-only for Static resources.
    void assertEditable() const;

private:
    NamespacedId _id;
    std::filesystem::path _filePath;
    ResourceMode _mode;
};

} // namespace noix::core
