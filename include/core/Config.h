#pragma once

#include "core/Value.h"
#include <cstdint>
#include <optional>
#include <string>

namespace noix::core {

class ConfigManager;

class Config {
public:
    Config();
    ~Config() = default;

    Config(Config&& other) noexcept = default;
    Config& operator=(Config&& other) noexcept = default;
    Config(const Config& other) = default;
    Config& operator=(const Config& other) = default;

    // --- 查询 ---
    bool has(const std::string& key) const;

    // getter (optional 版本)
    std::optional<std::string> getString(const std::string& key) const;
    std::optional<int64_t> getInt(const std::string& key) const;
    std::optional<double> getDouble(const std::string& key) const;
    std::optional<bool> getBool(const std::string& key) const;
    Config getObject(const std::string& key) const;

    // getter (default 版本)
    std::string getString(const std::string& key,
                          const std::string& defaultValue) const;
    int64_t getInt(const std::string& key, int64_t defaultValue) const;
    double getDouble(const std::string& key, double defaultValue) const;
    bool getBool(const std::string& key, bool defaultValue) const;

    // --- 设置 ---
    void setString(const std::string& key, const std::string& value);
    void setInt(const std::string& key, int64_t value);
    void setDouble(const std::string& key, double value);
    void setBool(const std::string& key, bool value);
    void setObject(const std::string& key, Config child);

    // --- 删除 ---
    bool remove(const std::string& key);

    // --- 序列化 ---
    std::string toJson() const;

    // 空检查
    explicit operator bool() const;

private:
    explicit Config(Value data);

    Value _data{Value::object()};
    friend class ConfigManager;
};

} // namespace noix::core
