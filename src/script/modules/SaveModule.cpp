#include "script/NativeModules.h"
#include "runtime/Application.h"
#include "runtime/SaveManager.h"
#include "core/Logger.h"
#include "core/NamespacedId.h"
#include "quickjs.h"
#include <string>

using noix::runtime::Application;
using noix::runtime::SaveManager;
using noix::core::NamespacedId;

namespace {

static SaveManager& getMgr() {
    return Application::instance().saveManager();
}

static JSValue save_save(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_ThrowTypeError(ctx, "save requires 3 arguments (slot, path, data)");

    const char* slot = JS_ToCString(ctx, argv[0]);
    if (!slot) return JS_EXCEPTION;

    const char* pathStr = JS_ToCString(ctx, argv[1]);
    if (!pathStr) {
        JS_FreeCString(ctx, slot);
        return JS_EXCEPTION;
    }

    auto id = NamespacedId::parse(pathStr);
    JS_FreeCString(ctx, pathStr);

    size_t len;
    const char* data = JS_ToCStringLen(ctx, &len, argv[2]);
    if (!data) {
        JS_FreeCString(ctx, slot);
        return JS_EXCEPTION;
    }

    std::string dataStr(data, len);
    JS_FreeCString(ctx, data);

    bool ok = getMgr().save(slot, id, dataStr);
    JS_FreeCString(ctx, slot);

    return JS_NewBool(ctx, ok);
}

static JSValue save_load(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "load requires 2 arguments (slot, path)");

    const char* slot = JS_ToCString(ctx, argv[0]);
    if (!slot) return JS_EXCEPTION;

    const char* pathStr = JS_ToCString(ctx, argv[1]);
    if (!pathStr) {
        JS_FreeCString(ctx, slot);
        return JS_EXCEPTION;
    }

    auto id = NamespacedId::parse(pathStr);
    JS_FreeCString(ctx, pathStr);

    std::string data = getMgr().load(slot, id);
    JS_FreeCString(ctx, slot);

    if (data.empty()) return JS_NULL;

    return JS_NewStringLen(ctx, data.c_str(), data.size());
}

static JSValue save_exists(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "exists requires 2 arguments (slot, path)");

    const char* slot = JS_ToCString(ctx, argv[0]);
    if (!slot) return JS_EXCEPTION;

    const char* pathStr = JS_ToCString(ctx, argv[1]);
    if (!pathStr) {
        JS_FreeCString(ctx, slot);
        return JS_EXCEPTION;
    }

    auto id = NamespacedId::parse(pathStr);
    JS_FreeCString(ctx, pathStr);

    bool result = getMgr().exists(slot, id);
    JS_FreeCString(ctx, slot);

    return JS_NewBool(ctx, result);
}

static JSValue save_remove(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "remove requires 2 arguments (slot, path)");

    const char* slot = JS_ToCString(ctx, argv[0]);
    if (!slot) return JS_EXCEPTION;

    const char* pathStr = JS_ToCString(ctx, argv[1]);
    if (!pathStr) {
        JS_FreeCString(ctx, slot);
        return JS_EXCEPTION;
    }

    auto id = NamespacedId::parse(pathStr);
    JS_FreeCString(ctx, pathStr);

    bool result = getMgr().remove(slot, id);
    JS_FreeCString(ctx, slot);

    return JS_NewBool(ctx, result);
}

static JSValue save_deleteSlot(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "deleteSlot requires 1 argument (slot)");

    const char* slot = JS_ToCString(ctx, argv[0]);
    if (!slot) return JS_EXCEPTION;

    bool result = getMgr().deleteSlot(slot);
    JS_FreeCString(ctx, slot);

    return JS_NewBool(ctx, result);
}

static JSValue save_listSlots(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    auto slots = getMgr().listSlots();
    JSValue arr = JS_NewArray(ctx);
    for (size_t i = 0; i < slots.size(); ++i) {
        JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i),
                             JS_NewStringLen(ctx, slots[i].c_str(), slots[i].size()));
    }
    return arr;
}

static int save_module_init(JSContext* ctx, JSModuleDef* m) {
    JS_SetModuleExport(ctx, m, "save",      JS_NewCFunction(ctx, save_save,      "save",      3));
    JS_SetModuleExport(ctx, m, "load",      JS_NewCFunction(ctx, save_load,      "load",      2));
    JS_SetModuleExport(ctx, m, "exists",    JS_NewCFunction(ctx, save_exists,    "exists",    2));
    JS_SetModuleExport(ctx, m, "remove",    JS_NewCFunction(ctx, save_remove,    "remove",    2));
    JS_SetModuleExport(ctx, m, "deleteSlot", JS_NewCFunction(ctx, save_deleteSlot, "deleteSlot", 1));
    JS_SetModuleExport(ctx, m, "listSlots", JS_NewCFunction(ctx, save_listSlots, "listSlots", 0));
    return 0;
}

} // anonymous namespace

namespace noix::script {

void registerSaveModule(JSContext* ctx) {
    JSModuleDef* m = JS_NewCModule(ctx, "noix:save", save_module_init);
    if (!m) {
        core::Logger::instance().error("NativeModules: failed to register noix:save");
        return;
    }
    JS_AddModuleExport(ctx, m, "save");
    JS_AddModuleExport(ctx, m, "load");
    JS_AddModuleExport(ctx, m, "exists");
    JS_AddModuleExport(ctx, m, "remove");
    JS_AddModuleExport(ctx, m, "deleteSlot");
    JS_AddModuleExport(ctx, m, "listSlots");
}

} // namespace noix::script
