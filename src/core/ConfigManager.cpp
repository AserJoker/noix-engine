#include "core/ConfigManager.h"
#include "core/Logger.h"
#include <cJSON.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace noix::core {

ConfigManager::ConfigManager(const std::filesystem::path& configDir)
    : _configDir(configDir) {}

ConfigManager::~ConfigManager() {
    _entries.clear();
}

std::filesystem::path ConfigManager::entryPath(const NamespacedId& id) const {
    return _configDir / id.ns() / (id.name() + ".json");
}

void ConfigManager::saveToDisk(const NamespacedId& id,
                               const std::filesystem::path& path) {
    std::string json;
    {
        std::lock_guard lock(_mutex);
        auto it = _entries.find(id);
        if (it == _entries.end()) return;
        json = it->second.config.toJson();
    }

    std::filesystem::create_directories(path.parent_path());

    std::ofstream file(path);
    if (!file.is_open()) {
        Logger::instance().error("Failed to write config file: {}", path.string());
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

bool ConfigManager::has(const NamespacedId& id) const {
    std::lock_guard lock(_mutex);
    return _entries.find(id) != _entries.end();
}

Config ConfigManager::get(const NamespacedId& id) const {
    std::lock_guard lock(_mutex);
    auto it = _entries.find(id);
    if (it == _entries.end()) return Config(static_cast<cJSON*>(nullptr));
    return it->second.config;
}

std::vector<NamespacedId> ConfigManager::list() const {
    std::lock_guard lock(_mutex);
    std::vector<NamespacedId> result;
    result.reserve(_entries.size());
    for (const auto& [id, _] : _entries) {
        result.push_back(id);
    }
    return result;
}

Config ConfigManager::getOrDefault(const NamespacedId& id, const Config& defaultCfg) {
    std::filesystem::path path;
    {
        std::lock_guard lock(_mutex);
        auto it = _entries.find(id);
        if (it != _entries.end()) return it->second.config;

        _entries.emplace(id, Entry{Config(defaultCfg), true});
        path = entryPath(id);
        Logger::instance().info("Created default config: {}:{}", id.ns(), id.name());
    }
    saveToDisk(id, path);
    return get(id);
}

// --- 创建/写入 ---

void ConfigManager::set(const NamespacedId& id, Config cfg) {
    std::filesystem::path path;
    {
        std::lock_guard lock(_mutex);
        auto it = _entries.find(id);
        if (it != _entries.end()) {
            it->second.config = std::move(cfg);
            it->second.dirty = true;
        } else {
            _entries.emplace(id, Entry{std::move(cfg), true});
        }
        path = entryPath(id);
    }
    saveToDisk(id, path);
}

Config ConfigManager::fromJson(const std::string& json) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) root = cJSON_CreateObject();
    return Config(root);
}

// --- 删除 ---

bool ConfigManager::remove(const NamespacedId& id) {
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

bool ConfigManager::load(const NamespacedId& id) {
    auto path = entryPath(id);

    if (!std::filesystem::exists(path)) {
        Logger::instance().info("Config file not found: {}", path.string());
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        Logger::instance().error("Failed to open config file: {}", path.string());
        return false;
    }

    std::stringstream buf;
    buf << file.rdbuf();
    std::string content = buf.str();

    cJSON* root = cJSON_Parse(content.c_str());
    if (!root) {
        const char* errPtr = cJSON_GetErrorPtr();
        Logger::instance().error("Failed to parse config file {}: error at {}",
            path.string(), errPtr ? errPtr : "unknown");
        return false;
    }

    std::lock_guard lock(_mutex);
    auto it = _entries.find(id);
    if (it != _entries.end()) {
        it->second.config = Config(root);
        it->second.dirty = false;
    } else {
        _entries.emplace(id, Entry{Config(root), false});
    }

    Logger::instance().info("Loaded config: {}:{}", id.ns(), id.name());
    return true;
}

int ConfigManager::loadAll() {
    if (!std::filesystem::exists(_configDir)) {
        Logger::instance().info("Config directory not found: {}", _configDir.string());
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
            if (load(NamespacedId(ns, name))) {
                ++count;
            }
        }
    }

    Logger::instance().info("ConfigManager: loaded {} config(s) from {}", count, _configDir.string());
    return count;
}

bool ConfigManager::save(const NamespacedId& id) {
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
    std::vector<std::pair<NamespacedId, std::filesystem::path>> toSave;
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
        Logger::instance().info("ConfigManager: saved {} config(s)", count);
    }
    return count;
}

} // namespace noix::core
