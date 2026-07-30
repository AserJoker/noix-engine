#include "script/NativeModules.h"
#include "core/Logger.h"
#include "debug/DebugServer.h"
#include "debug/commands/ScriptCallbackCommand.h"
#include "quickjs.h"
#include <string>

using noix::core::Logger;

namespace {

static JSValue debug_registerCommand(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_ThrowTypeError(ctx, "registerCommand requires (name, version, handler)");

    /* Get name */
    const char* name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    std::string cmdName(name);
    JS_FreeCString(ctx, name);

    /* Get version */
    const char* ver = JS_ToCString(ctx, argv[1]);
    if (!ver) return JS_EXCEPTION;
    std::string cmdVersion(ver);
    JS_FreeCString(ctx, ver);

    /* Validate handler is a function */
    if (!JS_IsFunction(ctx, argv[2])) {
        return JS_ThrowTypeError(ctx, "handler must be a function");
    }

    /* Get ScriptEngine from runtime opaque */
    auto* engine = static_cast<noix::script::ScriptEngine*>(
        JS_GetRuntimeOpaque(JS_GetRuntime(ctx)));
    if (!engine || !engine->debugServer()) {
        return JS_ThrowInternalError(ctx, "DebugServer not available");
    }

    /* Store the JS callback in ScriptEngine (it DupValues internally) */
    engine->registerCallback(cmdName, ctx, argv[2]);

    /* Create ScriptCallbackCommand — lightweight, only holds name + version + engine ref */
    auto cmd = std::make_shared<noix::debug::ScriptCallbackCommand>(
        cmdName, cmdVersion, *engine);

    engine->debugServer()->addApi(cmd);

    return JS_UNDEFINED;
}

static int debug_module_init(JSContext* ctx, JSModuleDef* m) {
    JS_SetModuleExport(ctx, m, "registerCommand",
        JS_NewCFunction(ctx, debug_registerCommand, "registerCommand", 3));
    return 0;
}

} // anonymous namespace

namespace noix::script {

void registerDebugModule(JSContext* ctx) {
    JSModuleDef* m = JS_NewCModule(ctx, "noix:debug", debug_module_init);
    if (!m) {
        Logger::instance().error("NativeModules: failed to register noix:debug");
        return;
    }
    JS_AddModuleExport(ctx, m, "registerCommand");
}

} // namespace noix::script
