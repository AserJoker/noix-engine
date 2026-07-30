#include "runtime/ConfigManager.h"
#include "core/Logger.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace noix::runtime {

ConfigManager::ConfigManager(const std::filesystem::path& configDir)
    : _configDir(configDir) {}

ConfigManager::~ConfigManager() {
    _entries.clear();
}

std::filesystem::path ConfigManager::entryPath(const core::NamespacedId& id) const {
    return _configDir / id.ns() / (id.name() + ".json");
}

void ConfigManager::saveToDisk(const core::NamespacedId& id,
                               const std::filesystem::path& path) {
    std::string json;
    {
        std::lock_guard lock(_mutex);
        auto it = _entries.find(id);
        if (it == _entries.end()) return;
        json = it->second.config.dump();
    }

    std::filesystem::create_directories(path.parent_path());

    std::ofstream file(path);
    if (!file.is_open()) {
        core::Logger::instance().error("Failed to write config file: {}", path.string());
        return;
    }
    file << json;
    file.close();

    {
        std::lock_guard lock(_mutex);
        auto it = _entries.find(id);
        if (it != _entries.end()) it->second.dirty = false;
    }
}

// --- 查询 ---

bool ConfigManager::has(const core::NamespacedId& id) const {
    std::lock_guard lock(_mutex);
    return _entries.find(id) != _entries.end();
}

core::Value ConfigManager::get(const core::NamespacedId& id) const {
    std::lock_guard lock(_mutex);
    auto it = _entries.find(id);
    if (it == _entries.end()) return core::Value();
    return it->second.config;
}

std::vector<core::NamespacedId> ConfigManager::list() const {
    std::lock_guard lock(_mutex);
    std::vector<core::NamespacedId> result;
    result.reserve(_entries.size());
    for (const auto& [id, _] : _entries) {
        result.push_back(id);
    }
    return result;
}

core::Value ConfigManager::getOrDefault(const core::NamespacedId& id, const core::Value& defaultVal) {
    std::filesystem::path path;
    {
        std::lock_guard lock(_mutex);
        auto it = _entries.find(id);
        if (it != _entries.end()) return it->second.config;

        _entries.emplace(id, Entry{core::Value(defaultVal), true});
        path = entryPath(id);
        core::Logger::instance().info("Created default config: {}:{}", id.ns(), id.name());
    }
    saveToDisk(id, path);
    return get(id);
}

// --- 创建/写入 ---

void ConfigManager::set(const core::NamespacedId& id, core::Value val) {
    std::filesystem::path path;
    {
        std::lock_guard lock(_mutex);
        auto it = _entries.find(id);
        if (it != _entries.end()) {
            it->second.config = std::move(val);
            it->second.dirty = true;
        } else {
            _entries.emplace(id, Entry{std::move(val), true});
        }
        path = entryPath(id);
    }
    saveToDisk(id, path);
}

// --- 删除 ---

bool ConfigManager::remove(const core::NamespacedId& id) {
    std::filesystem::path path;
    {
        std::lock_guard lock(_mutex);
        auto it = _entries.find(id);
        if (it == _entries.end()) return false;
        path = entryPath(id);
        _entries.erase(it);
    }
    if (!path.empty() && std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }
    return true;
}

// --- 磁盘 I/O ---

bool ConfigManager::load(const core::NamespacedId& id) {
    auto path = entryPath(id);

    if (!std::filesystem::exists(path)) {
        core::Logger::instance().info("Config file not found: {}", path.string());
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        core::Logger::instance().error("Failed to open config file: {}", path.string());
        return false;
    }

    std::stringstream buf;
    buf << file.rdbuf();
    std::string content = buf.str();

    core::Value data = core::Value::parse(content);
    if (data.isNull()) {
        core::Logger::instance().error("Failed to parse config file {}", path.string());
        return false;
    }

    std::lock_guard lock(_mutex);
    auto it = _entries.find(id);
    if (it != _entries.end()) {
        it->second.config = std::move(data);
        it->second.dirty = false;
    } else {
        _entries.emplace(id, Entry{std::move(data), false});
    }

    core::Logger::instance().info("Loaded config: {}:{}", id.ns(), id.name());
    return true;
}

int ConfigManager::loadAll() {
    if (!std::filesystem::exists(_configDir)) {
        core::Logger::instance().info("Config directory not found: {}", _configDir.string());
        return 0;
    }

    int count = 0;
    for (const auto& nsEntry : std::filesystem::directory_iterator(_configDir)) {
        if (!nsEntry.is_directory()) continue;
        std::string ns = nsEntry.path().filename().string();

        for (const auto& fileEntry : std::filesystem::directory_iterator(nsEntry.path())) {
            if (!fileEntry.is_regular_file()) continue;
            if (fileEntry.path().extension() != ".json") continue;

            std::string name = fileEntry.path().stem().string();
            if (load(core::NamespacedId(ns, name))) {
                ++count;
            }
        }
    }

    core::Logger::instance().info("ConfigManager: loaded {} config(s) from {}", count, _configDir.string());
    return count;
}

bool ConfigManager::save(const core::NamespacedId& id) {
    std::lock_guard lock(_mutex);
    auto it = _entries.find(id);
    if (it == _entries.end()) return false;
    if (!it->second.dirty) return true;
    auto path = entryPath(id);
    lock.~lock_guard();

    saveToDisk(id, path);
    return true;
}

int ConfigManager::saveAll() {
    std::vector<std::pair<core::NamespacedId, std::filesystem::path>> toSave;
    {
        std::lock_guard lock(_mutex);
        for (auto& [id, entry] : _entries) {
            if (entry.dirty) {
                toSave.emplace_back(id, entryPath(id));
            }
        }
    }

    int count = 0;
    for (auto& [id, path] : toSave) {
        saveToDisk(id, path);
        ++count;
    }

    if (count > 0) {
        core::Logger::instance().info("ConfigManager: saved {} config(s)", count);
    }
    return count;
}

} // namespace noix::runtime
