#include "debug/ConfigGetCommand.h"
#include "core/ConfigManager.h"
#include <cJSON.h>

namespace noix::debug {

ConfigGetCommand::ConfigGetCommand(core::ConfigManager& configManager)
    : _configManager(configManager) {}

std::string ConfigGetCommand::execute(const std::string& arguments) {
    cJSON* args = cJSON_Parse(arguments.c_str());
    if (!args) return "error: invalid json arguments";

    cJSON* ns = cJSON_GetObjectItem(args, "namespace");
    cJSON* name = cJSON_GetObjectItem(args, "name");
    if (!ns || !cJSON_IsString(ns) || !name || !cJSON_IsString(name)) {
        cJSON_Delete(args);
        return "error: missing 'namespace' or 'name' field";
    }

    core::NamespacedId id(ns->valuestring, name->valuestring);
    core::Config cfg = _configManager.get(id);
    cJSON_Delete(args);

    if (!cfg) return "error: config not found";
    return cfg.toJson();
}

ConfigSetCommand::ConfigSetCommand(core::ConfigManager& configManager)
    : _configManager(configManager) {}

std::string ConfigSetCommand::execute(const std::string& arguments) {
    cJSON* args = cJSON_Parse(arguments.c_str());
    if (!args) return "error: invalid json arguments";

    cJSON* ns = cJSON_GetObjectItem(args, "namespace");
    cJSON* name = cJSON_GetObjectItem(args, "name");
    cJSON* data = cJSON_GetObjectItem(args, "data");
    if (!ns || !cJSON_IsString(ns) || !name || !cJSON_IsString(name)) {
        cJSON_Delete(args);
        return "error: missing 'namespace' or 'name' field";
    }
    if (!data || !cJSON_IsObject(data)) {
        cJSON_Delete(args);
        return "error: missing 'data' object";
    }

    core::NamespacedId id(ns->valuestring, name->valuestring);
    char* dataJson = cJSON_PrintUnformatted(data);
    std::string jsonStr(dataJson);
    cJSON_free(dataJson);
    cJSON_Delete(args);

    _configManager.set(id, core::ConfigManager::fromJson(jsonStr));
    return "ok";
}

ConfigRemoveCommand::ConfigRemoveCommand(core::ConfigManager& configManager)
    : _configManager(configManager) {}

std::string ConfigRemoveCommand::execute(const std::string& arguments) {
    cJSON* args = cJSON_Parse(arguments.c_str());
    if (!args) return "error: invalid json arguments";

    cJSON* ns = cJSON_GetObjectItem(args, "namespace");
    cJSON* name = cJSON_GetObjectItem(args, "name");
    if (!ns || !cJSON_IsString(ns) || !name || !cJSON_IsString(name)) {
        cJSON_Delete(args);
        return "error: missing 'namespace' or 'name' field";
    }

    core::NamespacedId id(ns->valuestring, name->valuestring);
    cJSON_Delete(args);

    return _configManager.remove(id) ? "ok" : "error: config not found";
}

ConfigSaveCommand::ConfigSaveCommand(core::ConfigManager& configManager)
    : _configManager(configManager) {}

std::string ConfigSaveCommand::execute(const std::string& arguments) {
    cJSON* args = cJSON_Parse(arguments.c_str());
    if (!args) {
        int count = _configManager.saveAll();
        return "saved " + std::to_string(count) + " config(s)";
    }

    cJSON* ns = cJSON_GetObjectItem(args, "namespace");
    cJSON* name = cJSON_GetObjectItem(args, "name");
    if (ns && cJSON_IsString(ns) && name && cJSON_IsString(name)) {
        core::NamespacedId id(ns->valuestring, name->valuestring);
        cJSON_Delete(args);
        return _configManager.save(id) ? "ok" : "error: save failed";
    }

    cJSON_Delete(args);
    int count = _configManager.saveAll();
    return "saved " + std::to_string(count) + " config(s)";
}

ConfigListCommand::ConfigListCommand(core::ConfigManager& configManager)
    : _configManager(configManager) {}

std::string ConfigListCommand::execute(const std::string&) {
    auto entries = _configManager.list();
    cJSON* arr = cJSON_CreateArray();
    for (const auto& id : entries) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(id.toString().c_str()));
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "configs", arr);
    char* json = cJSON_PrintUnformatted(root);
    std::string result(json);
    cJSON_Delete(root);
    cJSON_free(json);
    return result;
}

} // namespace noix::debug
