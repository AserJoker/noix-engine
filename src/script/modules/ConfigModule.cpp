#include "script/NativeModules.h"
#include "runtime/Application.h"
#include "runtime/ConfigManager.h"
#include "script/ScriptEngine.h"
#include "core/Logger.h"
#include "core/NamespacedId.h"
#include "core/Value.h"
#include "quickjs.h"
#include <string>

using noix::runtime::Application;
using noix::runtime::ConfigManager;
using noix::core::NamespacedId;
using noix::core::Value;

namespace {

static ConfigManager& getMgr() {
    return Application::instance().configManager();
}

static JSValue config_get(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "get requires 2 arguments (id, defaultValue)");

    const char* idStr = JS_ToCString(ctx, argv[0]);
    if (!idStr) return JS_EXCEPTION;

    auto id = NamespacedId::parse(idStr);
    JS_FreeCString(ctx, idStr);

    Value defaultVal = noix::script::ScriptEngine::jsValueToValue(ctx, argv[1]);
    Value cfg = getMgr().getOrDefault(id, defaultVal);

    return noix::script::ScriptEngine::valueToJsValue(ctx, cfg);
}

static JSValue config_set(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "set requires 2 arguments (id, object)");

    const char* idStr = JS_ToCString(ctx, argv[0]);
    if (!idStr) return JS_EXCEPTION;

    auto id = NamespacedId::parse(idStr);
    JS_FreeCString(ctx, idStr);

    Value val = noix::script::ScriptEngine::jsValueToValue(ctx, argv[1]);
    getMgr().set(id, std::move(val));

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
