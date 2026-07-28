#include "core/Config.h"

namespace noix::core {

Config::Config()
    : _data(Value::object()) {}

Config::Config(Value data)
    : _data(std::move(data)) {}

// --- 查询 ---

bool Config::has(const std::string& key) const {
    return _data.has(key);
}

std::optional<std::string> Config::getString(const std::string& key) const {
    if (!_data.has(key)) return std::nullopt;
    const Value& v = _data[key];
    return v.isString() ? std::optional<std::string>(v.asString()) : std::nullopt;
}

std::optional<int64_t> Config::getInt(const std::string& key) const {
    if (!_data.has(key)) return std::nullopt;
    const Value& v = _data[key];
    return v.isNumber() ? std::optional<int64_t>(static_cast<int64_t>(v.asDouble())) : std::nullopt;
}

std::optional<double> Config::getDouble(const std::string& key) const {
    if (!_data.has(key)) return std::nullopt;
    const Value& v = _data[key];
    return v.isNumber() ? std::optional<double>(v.asDouble()) : std::nullopt;
}

std::optional<bool> Config::getBool(const std::string& key) const {
    if (!_data.has(key)) return std::nullopt;
    const Value& v = _data[key];
    return v.isBool() ? std::optional<bool>(v.asBool()) : std::nullopt;
}

Config Config::getObject(const std::string& key) const {
    if (!_data.has(key)) return Config(Value());
    const Value& v = _data[key];
    return v.isObject() ? Config(v) : Config(Value());
}

// getter (default 版本)

std::string Config::getString(const std::string& key,
                               const std::string& defaultValue) const {
    return getString(key).value_or(defaultValue);
}

int64_t Config::getInt(const std::string& key, int64_t defaultValue) const {
    return getInt(key).value_or(defaultValue);
}

double Config::getDouble(const std::string& key, double defaultValue) const {
    return getDouble(key).value_or(defaultValue);
}

bool Config::getBool(const std::string& key, bool defaultValue) const {
    return getBool(key).value_or(defaultValue);
}

// --- 设置 ---

void Config::setString(const std::string& key, const std::string& value) {
    auto& m = _data.asObject();
    m[key] = Value(value);
}

void Config::setInt(const std::string& key, int64_t value) {
    auto& m = _data.asObject();
    m[key] = Value(static_cast<int>(value));
}

void Config::setDouble(const std::string& key, double value) {
    auto& m = _data.asObject();
    m[key] = Value(value);
}

void Config::setBool(const std::string& key, bool value) {
    auto& m = _data.asObject();
    m[key] = Value(value);
}

void Config::setObject(const std::string& key, Config child) {
    auto& m = _data.asObject();
    m[key] = std::move(child._data);
}

// --- 删除 ---

bool Config::remove(const std::string& key) {
    auto& m = _data.asObject();
    auto it = m.find(key);
    if (it == m.end()) return false;
    m.erase(it);
    return true;
}

// --- 序列化 ---

std::string Config::toJson() const {
    return _data.dump();
}

Config::operator bool() const {
    return _data.isObject();
}

} // namespace noix::core
