#include "script/NativeModules.h"
#include "runtime/Application.h"
#include "core/ConfigManager.h"
#include "core/Logger.h"
#include "core/NamespacedId.h"
#include "quickjs.h"
#include <string>

using noix::runtime::Application;
using noix::core::ConfigManager;
using noix::core::Config;
using noix::core::NamespacedId;

namespace {

static ConfigManager& getMgr() {
    return Application::instance().configManager();
}

/**
 * Helper: convert a JS value to JSON string via JS_JSONStringify.
 * Caller must free the returned string with JS_FreeCString + JS_FreeValue.
 * Returns empty string on error.
 */
static std::string jsToJson(JSContext* ctx, JSValueConst val) {
    JSValue jsonVal = JS_JSONStringify(ctx, val, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(jsonVal)) return "";

    size_t len;
    const char* cstr = JS_ToCStringLen(ctx, &len, jsonVal);
    std::string result;
    if (cstr) result.assign(cstr, len);
    JS_FreeCString(ctx, cstr);
    JS_FreeValue(ctx, jsonVal);
    return result;
}

static JSValue config_get(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "get requires 2 arguments (id, defaultValue)");

    const char* idStr = JS_ToCString(ctx, argv[0]);
    if (!idStr) return JS_EXCEPTION;

    auto id = NamespacedId::parse(idStr);
    JS_FreeCString(ctx, idStr);

    std::string json = jsToJson(ctx, argv[1]);
    if (json.empty()) return JS_EXCEPTION;

    Config defaultCfg = ConfigManager::fromJson(json);
    Config cfg = getMgr().getOrDefault(id, std::move(defaultCfg));

    std::string resultJson = cfg.toJson();
    return JS_ParseJSON(ctx, resultJson.c_str(), resultJson.size(), "<config>");
}

static JSValue config_set(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "set requires 2 arguments (id, object)");

    const char* idStr = JS_ToCString(ctx, argv[0]);
    if (!idStr) return JS_EXCEPTION;

    auto id = NamespacedId::parse(idStr);
    JS_FreeCString(ctx, idStr);

    std::string json = jsToJson(ctx, argv[1]);
    if (json.empty()) return JS_EXCEPTION;

    Config cfg = ConfigManager::fromJson(json);
    getMgr().set(id, std::move(cfg));

    return JS_UNDEFINED;
}

static JSValue config_has(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "has requires 1 argument (id)");

    const char* idStr = JS_ToCString(ctx, argv[0]);
    if (!idStr) return JS_EXCEPTION;

    auto id = NamespacedId::parse(idStr);
    JS_FreeCString(ctx, idStr);

    return JS_NewBool(ctx, getMgr().has(id));
}

static JSValue config_remove(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "remove requires 1 argument (id)");

    const char* idStr = JS_ToCString(ctx, argv[0]);
    if (!idStr) return JS_EXCEPTION;

    auto id = NamespacedId::parse(idStr);
    JS_FreeCString(ctx, idStr);

    return JS_NewBool(ctx, getMgr().remove(id));
}

static JSValue config_list(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    auto ids = getMgr().list();
    JSValue arr = JS_NewArray(ctx);
    for (size_t i = 0; i < ids.size(); ++i) {
        std::string str = ids[i].ns() + ":" + ids[i].name();
        JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i),
                             JS_NewStringLen(ctx, str.c_str(), str.size()));
    }
    return arr;
}

static int config_module_init(JSContext* ctx, JSModuleDef* m) {
    JS_SetModuleExport(ctx, m, "get",    JS_NewCFunction(ctx, config_get,    "get",    2));
    JS_SetModuleExport(ctx, m, "set",    JS_NewCFunction(ctx, config_set,    "set",    2));
    JS_SetModuleExport(ctx, m, "has",    JS_NewCFunction(ctx, config_has,    "has",    1));
    JS_SetModuleExport(ctx, m, "remove", JS_NewCFunction(ctx, config_remove, "remove", 1));
    JS_SetModuleExport(ctx, m, "list",   JS_NewCFunction(ctx, config_list,   "list",   0));
    return 0;
}

} // anonymous namespace

namespace noix::script {

void registerConfigModule(JSContext* ctx) {
    JSModuleDef* m = JS_NewCModule(ctx, "noix:config", config_module_init);
    if (!m) {
        core::Logger::instance().error("NativeModules: failed to register noix:config");
        return;
    }
    JS_AddModuleExport(ctx, m, "get");
    JS_AddModuleExport(ctx, m, "set");
    JS_AddModuleExport(ctx, m, "has");
    JS_AddModuleExport(ctx, m, "remove");
    JS_AddModuleExport(ctx, m, "list");
}

} // namespace noix::script
