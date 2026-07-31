#include "script/NativeModules.h"
#include "runtime/Application.h"
#include "runtime/ModManager.h"
#include "core/Logger.h"
#include "core/Value.h"
#include "script/ScriptEngine.h"
#include "quickjs.h"
#include <string>

using noix::runtime::Application;
using noix::runtime::ModManager;

namespace {

static JSValue modmanager_listMods(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    auto* engine = static_cast<noix::script::ScriptEngine*>(
        JS_GetRuntimeOpaque(JS_GetRuntime(ctx)));
    auto* bus = engine->eventBus();
    // Access ModManager through Application
    auto& mgr = Application::instance().modManager();

    auto names = mgr.listMods();
    JSValue arr = JS_NewArray(ctx);
    uint32_t idx = 0;
    for (auto& name : names) {
        auto* info = mgr.getMod(name);
        JSValue obj = JS_NewObject(ctx);

        JS_SetPropertyStr(ctx, obj, "name", JS_NewString(ctx, info->name.c_str()));
        JS_SetPropertyStr(ctx, obj, "displayName", JS_NewString(ctx, info->displayName.c_str()));
        JS_SetPropertyStr(ctx, obj, "description", JS_NewString(ctx, info->description.c_str()));
        JS_SetPropertyStr(ctx, obj, "version", JS_NewString(ctx, info->version.c_str()));
        JS_SetPropertyStr(ctx, obj, "enabled", JS_NewBool(ctx, mgr.isEnabled(name)));

        JS_SetPropertyUint32(ctx, arr, idx++, obj);
    }
    return arr;
}

static JSValue modmanager_enable(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "enable requires (name)");

    const char* name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    std::string modName(name);
    JS_FreeCString(ctx, name);

    auto& mgr = Application::instance().modManager();
    bool ok = mgr.enable(modName);
    return JS_NewBool(ctx, ok);
}

static JSValue modmanager_disable(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "disable requires (name)");

    const char* name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    std::string modName(name);
    JS_FreeCString(ctx, name);

    auto& mgr = Application::instance().modManager();
    bool ok = mgr.disable(modName);
    return JS_NewBool(ctx, ok);
}

static JSValue modmanager_commit(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    auto& mgr = Application::instance().modManager();
    bool ok = mgr.commit();
    return JS_NewBool(ctx, ok);
}

static JSValue modmanager_rollback(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    auto& mgr = Application::instance().modManager();
    mgr.rollback();
    return JS_UNDEFINED;
}

static int modmanager_module_init(JSContext* ctx, JSModuleDef* m) {
    JS_SetModuleExport(ctx, m, "listMods",
        JS_NewCFunction(ctx, modmanager_listMods, "listMods", 0));
    JS_SetModuleExport(ctx, m, "enable",
        JS_NewCFunction(ctx, modmanager_enable, "enable", 1));
    JS_SetModuleExport(ctx, m, "disable",
        JS_NewCFunction(ctx, modmanager_disable, "disable", 1));
    JS_SetModuleExport(ctx, m, "commit",
        JS_NewCFunction(ctx, modmanager_commit, "commit", 0));
    JS_SetModuleExport(ctx, m, "rollback",
        JS_NewCFunction(ctx, modmanager_rollback, "rollback", 0));
    return 0;
}

} // anonymous namespace

namespace noix::script {

void registerModManagerModule(JSContext* ctx) {
    JSModuleDef* m = JS_NewCModule(ctx, "noix:modmanager", modmanager_module_init);
    if (!m) {
        core::Logger::instance().error("NativeModules: failed to register noix:modmanager");
        return;
    }
    JS_AddModuleExport(ctx, m, "listMods");
    JS_AddModuleExport(ctx, m, "enable");
    JS_AddModuleExport(ctx, m, "disable");
    JS_AddModuleExport(ctx, m, "commit");
    JS_AddModuleExport(ctx, m, "rollback");
}

} // namespace noix::script
