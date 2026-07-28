#include "core/Value.h"
#include <cJSON.h>

namespace noix::core {

// ---- Internal: Value → cJSON tree (uses only public API) ----

static cJSON* valueToCjson(const Value& v) {
    if (v.isNull())  return cJSON_CreateNull();
    if (v.isBool())  return v.asBool() ? cJSON_CreateTrue() : cJSON_CreateFalse();
    if (v.isNumber()) return cJSON_CreateNumber(v.asDouble());
    if (v.isString()) return cJSON_CreateString(v.asString().c_str());

    if (v.isArray()) {
        cJSON* arr = cJSON_CreateArray();
        for (auto& item : v.asArray()) {
            cJSON_AddItemToArray(arr, valueToCjson(item));
        }
        return arr;
    }

    if (v.isObject()) {
        cJSON* obj = cJSON_CreateObject();
        for (auto& [key, val] : v.asObject()) {
            cJSON_AddItemToObject(obj, key.c_str(), valueToCjson(val));
        }
        return obj;
    }

    return cJSON_CreateNull();
}

// ---- Internal: cJSON tree → Value (recursive) ----

static Value cjsonToValue(cJSON* node) {
    if (!node) return Value();

    if (cJSON_IsNull(node))   return Value();
    if (cJSON_IsBool(node))   return Value(cJSON_IsTrue(node));
    if (cJSON_IsNumber(node)) {
        double d = node->valuedouble;
        if (d == static_cast<int>(d) && d >= INT_MIN && d <= INT_MAX) {
            return Value(static_cast<int>(d));
        }
        return Value(d);
    }
    if (cJSON_IsString(node)) return Value(std::string(node->valuestring));

    if (cJSON_IsArray(node)) {
        std::vector<Value> items;
        int size = cJSON_GetArraySize(node);
        items.reserve(size);
        for (int i = 0; i < size; ++i) {
            items.push_back(cjsonToValue(cJSON_GetArrayItem(node, i)));
        }
        return Value::array(std::move(items));
    }

    if (cJSON_IsObject(node)) {
        std::map<std::string, Value> props;
        cJSON* child = node->child;
        while (child) {
            props.emplace(child->string, cjsonToValue(child));
            child = child->next;
        }
        return Value::object(std::move(props));
    }

    return Value();
}

// ---- Public serialization ----

std::string Value::dump() const {
    cJSON* root = valueToCjson(*this);
    char* s = cJSON_PrintUnformatted(root);
    std::string result(s);
    cJSON_free(s);
    cJSON_Delete(root);
    return result;
}

Value Value::parse(const std::string& json) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) return Value();
    Value result = cjsonToValue(root);
    cJSON_Delete(root);
    return result;
}

} // namespace noix::core
