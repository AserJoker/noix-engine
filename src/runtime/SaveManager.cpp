#include "runtime/SaveManager.h"
#include "core/Logger.h"

#include <fstream>
#include <filesystem>

namespace noix::runtime {

SaveManager::SaveManager(const std::filesystem::path& basePath)
    : _savesDir(basePath / "saves") {}

std::filesystem::path SaveManager::resolvePath(const std::string& slot,
                                               const core::NamespacedId& id) const {
    return _savesDir / slot / id.ns() / id.name();
}

bool SaveManager::save(const std::string& slot,
                       const core::NamespacedId& path,
                       const std::string& data) {
    auto filePath = resolvePath(slot, path);

    std::error_code ec;
    std::filesystem::create_directories(filePath.parent_path(), ec);
    if (ec) {
        core::Logger::instance().error("SaveManager: failed to create directories for '{}': {}",
                                       filePath.string(), ec.message());
        return false;
    }

    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        core::Logger::instance().error("SaveManager: failed to open '{}' for writing",
                                       filePath.string());
        return false;
    }

    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!file) {
        core::Logger::instance().error("SaveManager: write failed for '{}'",
                                       filePath.string());
        return false;
    }

    return true;
}

std::string SaveManager::load(const std::string& slot,
                              const core::NamespacedId& path) const {
    auto filePath = resolvePath(slot, path);

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};

    auto size = file.tellg();
    if (size == static_cast<std::streampos>(0)) return {};

    file.seekg(0);
    std::string result(static_cast<size_t>(size), '\0');
    file.read(result.data(), size);

    if (!file) return {};
    return result;
}

bool SaveManager::exists(const std::string& slot,
                         const core::NamespacedId& path) const {
    return std::filesystem::exists(resolvePath(slot, path));
}

bool SaveManager::remove(const std::string& slot,
                         const core::NamespacedId& path) {
    std::error_code ec;
    return std::filesystem::remove(resolvePath(slot, path), ec);
}

bool SaveManager::deleteSlot(const std::string& slot) {
    auto slotDir = _savesDir / slot;
    if (!std::filesystem::exists(slotDir)) return false;

    std::error_code ec;
    std::filesystem::remove_all(slotDir, ec);
    return !ec;
}

std::vector<std::string> SaveManager::listSlots() const {
    std::vector<std::string> slots;

    if (!std::filesystem::exists(_savesDir)) return slots;

    for (const auto& entry : std::filesystem::directory_iterator(_savesDir)) {
        if (entry.is_directory()) {
            slots.push_back(entry.path().filename().string());
        }
    }
    return slots;
}

} // namespace noix::runtime
