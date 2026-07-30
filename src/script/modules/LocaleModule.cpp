#include "script/NativeModules.h"
#include "runtime/Locale.h"
#include "core/Logger.h"
#include "quickjs.h"
#include <string>

using noix::runtime::Locale;

namespace {

static JSValue locale_i18n(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "i18n requires at least 1 argument (key)");

    const char* key = JS_ToCString(ctx, argv[0]);
    if (!key) return JS_EXCEPTION;

    std::string defaultValue;
    if (argc >= 2) {
        const char* def = JS_ToCString(ctx, argv[1]);
        if (def) {
            defaultValue = def;
            JS_FreeCString(ctx, def);
        }
    }

    std::string result = Locale::instance().i18n(key, defaultValue);
    JS_FreeCString(ctx, key);

    return JS_NewStringLen(ctx, result.c_str(), result.size());
}

static JSValue locale_setLang(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "setLang requires 1 argument (lang)");

    const char* lang = JS_ToCString(ctx, argv[0]);
    if (!lang) return JS_EXCEPTION;

    Locale::instance().setLang(lang);
    JS_FreeCString(ctx, lang);

    return JS_UNDEFINED;
}

static JSValue locale_getLang(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    const auto& lang = Locale::instance().lang();
    return JS_NewStringLen(ctx, lang.c_str(), lang.size());
}

static JSValue locale_addNamespace(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "addNamespace requires 1 argument (namespace)");
    const char* ns = JS_ToCString(ctx, argv[0]);
    if (!ns) return JS_EXCEPTION;
    Locale::instance().addNamespace(ns);
    JS_FreeCString(ctx, ns);
    return JS_UNDEFINED;
}

static JSValue locale_removeNamespace(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "removeNamespace requires 1 argument (namespace)");
    const char* ns = JS_ToCString(ctx, argv[0]);
    if (!ns) return JS_EXCEPTION;
    Locale::instance().removeNamespace(ns);
    JS_FreeCString(ctx, ns);
    return JS_UNDEFINED;
}

static JSValue locale_reset(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    Locale::instance().reset();
    return JS_UNDEFINED;
}

static int locale_module_init(JSContext* ctx, JSModuleDef* m) {
    JS_SetModuleExport(ctx, m, "i18n",           JS_NewCFunction(ctx, locale_i18n,           "i18n",           2));
    JS_SetModuleExport(ctx, m, "setLang",         JS_NewCFunction(ctx, locale_setLang,        "setLang",         1));
    JS_SetModuleExport(ctx, m, "getLang",         JS_NewCFunction(ctx, locale_getLang,        "getLang",         0));
    JS_SetModuleExport(ctx, m, "addNamespace",    JS_NewCFunction(ctx, locale_addNamespace,   "addNamespace",    1));
    JS_SetModuleExport(ctx, m, "removeNamespace", JS_NewCFunction(ctx, locale_removeNamespace,"removeNamespace", 1));
    JS_SetModuleExport(ctx, m, "reset",           JS_NewCFunction(ctx, locale_reset,          "reset",           0));
    return 0;
}

} // anonymous namespace

namespace noix::script {

void registerLocaleModule(JSContext* ctx) {
    JSModuleDef* m = JS_NewCModule(ctx, "noix:locale", locale_module_init);
    if (!m) {
        core::Logger::instance().error("NativeModules: failed to register noix:locale");
        return;
    }
    JS_AddModuleExport(ctx, m, "i18n");
    JS_AddModuleExport(ctx, m, "setLang");
    JS_AddModuleExport(ctx, m, "getLang");
    JS_AddModuleExport(ctx, m, "addNamespace");
    JS_AddModuleExport(ctx, m, "removeNamespace");
    JS_AddModuleExport(ctx, m, "reset");
}

} // namespace noix::script
