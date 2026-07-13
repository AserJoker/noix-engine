#pragma once

#include "core/Config.h"
#include "core/NamespacedId.h"
#include <filesystem>
#include <map>
#include <mutex>
#include <vector>

namespace noix::core {

class ConfigManager {
public:
    explicit ConfigManager(const std::filesystem::path& configDir);
    ~ConfigManager();

    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    // --- 查询 ---
    bool has(const NamespacedId& id) const;
    Config get(const NamespacedId& id) const;
    std::vector<NamespacedId> list() const;

    /// 获取配置，不存在则用默认值创建并标记为 dirty（下次 saveAll 会写入磁盘）
    Config getOrDefault(const NamespacedId& id, const Config& defaultCfg);

    // --- 创建/写入 ---
    void set(const NamespacedId& id, Config cfg);

    /// 从 JSON 字符串创建 Config 对象
    static Config fromJson(const std::string& json);

    // --- 删除 ---
    bool remove(const NamespacedId& id);

    // --- 磁盘 I/O ---
    bool load(const NamespacedId& id);
    int loadAll();
    bool save(const NamespacedId& id);
    int saveAll();

    const std::filesystem::path& configDir() const { return _configDir; }

private:
    struct Entry {
        Config config;
        bool dirty = false;
    };

    std::filesystem::path entryPath(const NamespacedId& id) const;

    std::filesystem::path _configDir;
    mutable std::mutex _mutex;
    std::map<NamespacedId, Entry> _entries;
};

} // namespace noix::core
