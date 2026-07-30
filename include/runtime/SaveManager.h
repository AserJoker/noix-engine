#pragma once

/*
 * SaveManager — Persistent save-data storage for the engine.
 *
 * Save files live under <basePath>/saves/<slot>/<namespace>/...
 * The path within a namespace is fully controlled by the caller.
 *
 * Example:
 *   saver.save("world1", NamespacedId("noix", "player/inventory.dat"), data);
 *   → writes to saves/world1/noix/player/inventory.dat
 *
 * Directories are created recursively on write.
 */

#include "core/NamespacedId.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace noix::runtime {

class SaveManager {
public:
    explicit SaveManager(const std::filesystem::path& basePath);

    /// Write data to a save file. Creates directories recursively.
    /// Returns true on success.
    bool save(const std::string& slot,
              const core::NamespacedId& path,
              const std::string& data);

    /// Read data from a save file. Returns empty string if not found.
    std::string load(const std::string& slot,
                     const core::NamespacedId& path) const;

    /// Check if a save file exists.
    bool exists(const std::string& slot,
                const core::NamespacedId& path) const;

    /// Remove a single save file. Returns true if the file existed.
    bool remove(const std::string& slot,
                const core::NamespacedId& path);

    /// Delete an entire save slot (all files under saves/<slot>).
    bool deleteSlot(const std::string& slot);

    /// List all save slot names.
    std::vector<std::string> listSlots() const;

private:
    std::filesystem::path resolvePath(const std::string& slot,
                                      const core::NamespacedId& id) const;

    std::filesystem::path _savesDir;
};

} // namespace noix::runtime
