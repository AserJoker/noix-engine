#pragma once

#include "core/NamespacedId.h"
#include "core/Value.h"
#include <filesystem>
#include <map>
#include <mutex>
#include <vector>

namespace noix::runtime {

class ConfigManager {
public:
    explicit ConfigManager(const std::filesystem::path& configDir);
    ~ConfigManager();

    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    // --- 查询 ---
    bool has(const core::NamespacedId& id) const;
    core::Value get(const core::NamespacedId& id) const;
    std::vector<core::NamespacedId> list() const;

    /// 获取配置，不存在则用默认值创建并立即写入磁盘
    core::Value getOrDefault(const core::NamespacedId& id, const core::Value& defaultVal);

    // --- 创建/写入 ---
    void set(const core::NamespacedId& id, core::Value val);

    // --- 删除 ---
    bool remove(const core::NamespacedId& id);

    // --- 磁盘 I/O ---
    bool load(const core::NamespacedId& id);
    int loadAll();
    bool save(const core::NamespacedId& id);
    int saveAll();

    const std::filesystem::path& configDir() const { return _configDir; }

private:
    struct Entry {
        core::Value config;
        bool dirty = false;
    };

    std::filesystem::path entryPath(const core::NamespacedId& id) const;
    void saveToDisk(const core::NamespacedId& id, const std::filesystem::path& path);

    std::filesystem::path _configDir;
    mutable std::mutex _mutex;
    std::map<core::NamespacedId, Entry> _entries;
};

} // namespace noix::runtime
