#pragma once

/*
 * Value — JSON value wrapper using std::variant internally.
 * Only interacts with cJSON during dump()/parse() for serialization.
 * Internally stores: std::monostate (null), bool, int, double,
 * std::string, std::map<std::string, Value>, std::vector<Value>.
 */

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace noix::core {

class Value {
public:
    Value() : _data(std::monostate{}) {}
    Value(bool v) : _data(v) {}
    Value(int v) : _data(v) {}
    Value(double v) : _data(v) {}
    Value(const char* v) : _data(std::string(v)) {}
    Value(const std::string& v) : _data(v) {}

    static Value object(std::map<std::string, Value> props = {}) {
        Value v;
        v._data = std::move(props);
        return v;
    }

    static Value array(std::vector<Value> items = {}) {
        Value v;
        v._data = std::move(items);
        return v;
    }

    // Type queries
    bool isNull() const { return std::holds_alternative<std::monostate>(_data); }
    bool isBool() const { return std::holds_alternative<bool>(_data); }
    bool isNumber() const { return std::holds_alternative<int>(_data) || std::holds_alternative<double>(_data); }
    bool isString() const { return std::holds_alternative<std::string>(_data); }
    bool isArray() const { return std::holds_alternative<std::vector<Value>>(_data); }
    bool isObject() const { return std::holds_alternative<std::map<std::string, Value>>(_data); }

    // Value access (no default — returns 0/""/false for wrong type)
    bool asBool() const { return isBool() ? std::get<bool>(_data) : false; }
    int asInt() const {
        if (std::holds_alternative<int>(_data)) return std::get<int>(_data);
        if (std::holds_alternative<double>(_data)) return static_cast<int>(std::get<double>(_data));
        return 0;
    }
    double asDouble() const {
        if (std::holds_alternative<double>(_data)) return std::get<double>(_data);
        if (std::holds_alternative<int>(_data)) return static_cast<double>(std::get<int>(_data));
        return 0.0;
    }
    std::string asString() const {
        return std::holds_alternative<std::string>(_data) ? std::get<std::string>(_data) : std::string{};
    }

    // Value access with explicit default
    bool asBool(bool defaultValue) const {
        return isBool() ? std::get<bool>(_data) : defaultValue;
    }
    int asInt(int defaultValue) const {
        if (std::holds_alternative<int>(_data)) return std::get<int>(_data);
        if (std::holds_alternative<double>(_data)) return static_cast<int>(std::get<double>(_data));
        return defaultValue;
    }
    double asDouble(double defaultValue) const {
        if (std::holds_alternative<double>(_data)) return std::get<double>(_data);
        if (std::holds_alternative<int>(_data)) return static_cast<double>(std::get<int>(_data));
        return defaultValue;
    }
    std::string asString(const std::string &defaultValue) const {
        return std::holds_alternative<std::string>(_data) ? std::get<std::string>(_data) : defaultValue;
    }

    const std::map<std::string, Value>& asObject() const {
        return std::get<std::map<std::string, Value>>(_data);
    }
    std::map<std::string, Value>& asObject() {
        return std::get<std::map<std::string, Value>>(_data);
    }

    const std::vector<Value>& asArray() const {
        return std::get<std::vector<Value>>(_data);
    }
    std::vector<Value>& asArray() {
        return std::get<std::vector<Value>>(_data);
    }

    // Object access
    Value operator[](const std::string& key) const {
        if (!isObject()) return Value();
        auto& m = std::get<std::map<std::string, Value>>(_data);
        auto it = m.find(key);
        return it != m.end() ? it->second : Value();
    }

    bool has(const std::string& key) const {
        if (!isObject()) return false;
        auto& m = std::get<std::map<std::string, Value>>(_data);
        return m.find(key) != m.end();
    }

    // Array access
    size_t size() const {
        if (isArray()) return std::get<std::vector<Value>>(_data).size();
        return 0;
    }

    Value operator[](size_t index) const {
        if (!isArray()) return Value();
        auto& v = std::get<std::vector<Value>>(_data);
        return index < v.size() ? v[index] : Value();
    }

    // Serialization — uses cJSON internally, returns JSON string
    std::string dump() const;

    // Deserialization — uses cJSON internally, returns Value
    static Value parse(const std::string& json);

    // Type discrimination for int vs double (both satisfy isNumber())
    bool isInt() const { return std::holds_alternative<int>(_data); }
    bool isDouble() const { return std::holds_alternative<double>(_data); }

private:
    std::variant<std::monostate, bool, int, double, std::string,
                 std::map<std::string, Value>, std::vector<Value>> _data;
};

} // namespace noix::core

/* User-defined literal: R"({...})"_json → Value */
noix::core::Value operator""_json(const char* str, size_t len);
