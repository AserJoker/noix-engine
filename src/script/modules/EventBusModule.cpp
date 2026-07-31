#include "script/NativeModules.h"
#include "runtime/Application.h"
#include "runtime/EventBus.h"
#include "script/ScriptEngine.h"
#include "core/Logger.h"
#include "core/Value.h"
#include "quickjs.h"
#include <string>

namespace {

static JSValue eventbus_addEventListener(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "addEventListener requires (eventName, callback)");

    const char* name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    std::string eventName(name);
    JS_FreeCString(ctx, name);

    if (!JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx, "callback must be a function");
    }

    JS_DupValue(ctx, argv[1]);
    void* callbackPtr = reinterpret_cast<void*>(JS_VALUE_GET_PTR(argv[1]));

    auto* engine = static_cast<noix::script::ScriptEngine*>(
        JS_GetRuntimeOpaque(JS_GetRuntime(ctx)));
    auto* bus = engine->eventBus();

    int handle = bus->addEventListener(eventName, ctx, callbackPtr);
    return JS_NewInt32(ctx, handle);
}

static JSValue eventbus_removeEventListener(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "removeEventListener requires (handle)");

    int32_t handle;
    if (JS_ToInt32(ctx, &handle, argv[0]) < 0) return JS_EXCEPTION;

    auto* engine = static_cast<noix::script::ScriptEngine*>(
        JS_GetRuntimeOpaque(JS_GetRuntime(ctx)));
    auto* bus = engine->eventBus();
    bus->removeEventListener(handle);

    return JS_UNDEFINED;
}

static JSValue eventbus_emit(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "emit requires (eventName[, data])");

    const char* name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    std::string eventName(name);
    JS_FreeCString(ctx, name);

    noix::core::Value data;
    if (argc >= 2) {
        data = noix::script::ScriptEngine::jsValueToValue(ctx, argv[1]);
    }
    if (data.isNull()) data = noix::core::Value::object();

    auto* engine = static_cast<noix::script::ScriptEngine*>(
        JS_GetRuntimeOpaque(JS_GetRuntime(ctx)));
    auto* bus = engine->eventBus();
    bus->emitAsync(eventName, data);

    return JS_UNDEFINED;
}

static int eventbus_module_init(JSContext* ctx, JSModuleDef* m) {
    JS_SetModuleExport(ctx, m, "addEventListener",
        JS_NewCFunction(ctx, eventbus_addEventListener, "addEventListener", 2));
    JS_SetModuleExport(ctx, m, "removeEventListener",
        JS_NewCFunction(ctx, eventbus_removeEventListener, "removeEventListener", 1));
    JS_SetModuleExport(ctx, m, "emit",
        JS_NewCFunction(ctx, eventbus_emit, "emit", 2));
    return 0;
}

} // anonymous namespace

namespace noix::script {

void registerEventBusModule(JSContext* ctx) {
    JSModuleDef* m = JS_NewCModule(ctx, "noix:eventbus", eventbus_module_init);
    if (!m) {
        core::Logger::instance().error("NativeModules: failed to register noix:eventbus");
        return;
    }
    JS_AddModuleExport(ctx, m, "addEventListener");
    JS_AddModuleExport(ctx, m, "removeEventListener");
    JS_AddModuleExport(ctx, m, "emit");
}

} // namespace noix::script
