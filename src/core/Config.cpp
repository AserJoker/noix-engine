#include "core/Config.h"
#include <cJSON.h>
#include <cstring>

namespace noix::core {

Config::Config()
    : _root(cJSON_CreateObject()) {}

Config::~Config() {
    if (_root) {
        cJSON_Delete(_root);
        _root = nullptr;
    }
}

Config::Config(Config&& other) noexcept
    : _root(other._root) {
    other._root = nullptr;
}

Config& Config::operator=(Config&& other) noexcept {
    if (this != &other) {
        if (_root) cJSON_Delete(_root);
        _root = other._root;
        other._root = nullptr;
    }
    return *this;
}

Config::Config(const Config& other)
    : _root(other._root ? cJSON_Duplicate(other._root, true) : nullptr) {}

Config& Config::operator=(const Config& other) {
    if (this != &other) {
        if (_root) cJSON_Delete(_root);
        _root = other._root ? cJSON_Duplicate(other._root, true) : nullptr;
    }
    return *this;
}

Config::Config(cJSON* root)
    : _root(root) {}

cJSON* Config::root() const { return _root; }

// --- 查询 ---

bool Config::has(const std::string& key) const {
    if (!_root) return false;
    return cJSON_HasObjectItem(_root, key.c_str());
}

std::optional<std::string> Config::getString(const std::string& key) const {
    if (!_root) return std::nullopt;
    cJSON* node = cJSON_GetObjectItemCaseSensitive(_root, key.c_str());
    if (!cJSON_IsString(node)) return std::nullopt;
    return std::string(node->valuestring);
}

std::optional<int64_t> Config::getInt(const std::string& key) const {
    if (!_root) return std::nullopt;
    cJSON* node = cJSON_GetObjectItemCaseSensitive(_root, key.c_str());
    if (!cJSON_IsNumber(node)) return std::nullopt;
    return static_cast<int64_t>(node->valuedouble);
}

std::optional<double> Config::getDouble(const std::string& key) const {
    if (!_root) return std::nullopt;
    cJSON* node = cJSON_GetObjectItemCaseSensitive(_root, key.c_str());
    if (!cJSON_IsNumber(node)) return std::nullopt;
    return node->valuedouble;
}

std::optional<bool> Config::getBool(const std::string& key) const {
    if (!_root) return std::nullopt;
    cJSON* node = cJSON_GetObjectItemCaseSensitive(_root, key.c_str());
    if (!cJSON_IsBool(node)) return std::nullopt;
    return cJSON_IsTrue(node);
}

Config Config::getObject(const std::string& key) const {
    if (!_root) return Config(static_cast<cJSON*>(nullptr));
    cJSON* node = cJSON_GetObjectItemCaseSensitive(_root, key.c_str());
    if (!cJSON_IsObject(node)) return Config(static_cast<cJSON*>(nullptr));
    return Config(cJSON_Duplicate(node, true));
}

// getter (default 版本)

std::string Config::getString(const std::string& key,
                               const std::string& defaultValue) const {
    auto opt = getString(key);
    return opt.value_or(defaultValue);
}

int64_t Config::getInt(const std::string& key, int64_t defaultValue) const {
    auto opt = getInt(key);
    return opt.value_or(defaultValue);
}

double Config::getDouble(const std::string& key, double defaultValue) const {
    auto opt = getDouble(key);
    return opt.value_or(defaultValue);
}

bool Config::getBool(const std::string& key, bool defaultValue) const {
    auto opt = getBool(key);
    return opt.value_or(defaultValue);
}

// --- 设置 ---

void Config::setString(const std::string& key, const std::string& value) {
    if (!_root) _root = cJSON_CreateObject();
    cJSON* existing = cJSON_GetObjectItemCaseSensitive(_root, key.c_str());
    if (existing && cJSON_IsString(existing)) {
        cJSON_SetValuestring(existing, value.c_str());
    } else {
        cJSON_DeleteItemFromObjectCaseSensitive(_root, key.c_str());
        cJSON_AddStringToObject(_root, key.c_str(), value.c_str());
    }
}

void Config::setInt(const std::string& key, int64_t value) {
    if (!_root) _root = cJSON_CreateObject();
    cJSON_DeleteItemFromObjectCaseSensitive(_root, key.c_str());
    cJSON_AddNumberToObject(_root, key.c_str(), static_cast<double>(value));
}

void Config::setDouble(const std::string& key, double value) {
    if (!_root) _root = cJSON_CreateObject();
    cJSON_DeleteItemFromObjectCaseSensitive(_root, key.c_str());
    cJSON_AddNumberToObject(_root, key.c_str(), value);
}

void Config::setBool(const std::string& key, bool value) {
    if (!_root) _root = cJSON_CreateObject();
    cJSON_DeleteItemFromObjectCaseSensitive(_root, key.c_str());
    cJSON_AddBoolToObject(_root, key.c_str(), value);
}

void Config::setObject(const std::string& key, Config child) {
    if (!_root) _root = cJSON_CreateObject();
    cJSON_DeleteItemFromObjectCaseSensitive(_root, key.c_str());
    if (child._root) {
        cJSON_AddItemToObject(_root, key.c_str(), cJSON_Duplicate(child._root, true));
    }
}

// --- 删除 ---

bool Config::remove(const std::string& key) {
    if (!_root) return false;
    if (!has(key)) return false;
    cJSON_DeleteItemFromObjectCaseSensitive(_root, key.c_str());
    return true;
}

// --- 序列化 ---

std::string Config::toJson() const {
    if (!_root) return "{}";
    char* raw = cJSON_Print(_root);
    std::string result(raw);
    cJSON_free(raw);
    return result;
}

Config::operator bool() const {
    return _root != nullptr;
}

} // namespace noix::core
