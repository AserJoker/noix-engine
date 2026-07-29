#include "script/NativeModules.h"
#include "core/Logger.h"
#include "quickjs.h"
#include <string>

using noix::core::Logger;
using noix::core::LogLevel;

namespace {

/* Concatenate all JS arguments into a single string, space-separated. */
std::string concatArgs(JSContext* ctx, int argc, JSValueConst* argv) {
    std::string result;
    for (int i = 0; i < argc; i++) {
        if (i > 0) result += ' ';
        const char* s = JS_ToCString(ctx, argv[i]);
        if (s) {
            result += s;
            JS_FreeCString(ctx, s);
        }
    }
    return result;
}

static JSValue logger_trace(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Logger::instance().log(LogLevel::Trace, "{}", concatArgs(ctx, argc, argv));
    return JS_UNDEFINED;
}

static JSValue logger_debug(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Logger::instance().log(LogLevel::Debug, "{}", concatArgs(ctx, argc, argv));
    return JS_UNDEFINED;
}

static JSValue logger_info(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Logger::instance().log(LogLevel::Info, "{}", concatArgs(ctx, argc, argv));
    return JS_UNDEFINED;
}

static JSValue logger_warn(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Logger::instance().log(LogLevel::Warn, "{}", concatArgs(ctx, argc, argv));
    return JS_UNDEFINED;
}

static JSValue logger_error(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Logger::instance().log(LogLevel::Error, "{}", concatArgs(ctx, argc, argv));
    return JS_UNDEFINED;
}

static JSValue logger_critical(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Logger::instance().log(LogLevel::Critical, "{}", concatArgs(ctx, argc, argv));
    return JS_UNDEFINED;
}

static JSValue logger_setLevel(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "setLevel requires 1 argument");
    const char* s = JS_ToCString(ctx, argv[0]);
    if (!s) return JS_EXCEPTION;

    LogLevel level = LogLevel::Info;
    if (std::string_view(s) == "trace")    level = LogLevel::Trace;
    else if (std::string_view(s) == "debug")    level = LogLevel::Debug;
    else if (std::string_view(s) == "info")     level = LogLevel::Info;
    else if (std::string_view(s) == "warn")     level = LogLevel::Warn;
    else if (std::string_view(s) == "error")    level = LogLevel::Error;
    else if (std::string_view(s) == "critical") level = LogLevel::Critical;

    JS_FreeCString(ctx, s);
    Logger::instance().setLevel(level);
    return JS_UNDEFINED;
}

static JSValue logger_getLevel(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    auto level = Logger::instance().level();
    const char* s = "info";
    switch (level) {
    case LogLevel::Trace:    s = "trace"; break;
    case LogLevel::Debug:    s = "debug"; break;
    case LogLevel::Info:     s = "info"; break;
    case LogLevel::Warn:     s = "warn"; break;
    case LogLevel::Error:    s = "error"; break;
    case LogLevel::Critical: s = "critical"; break;
    }
    return JS_NewString(ctx, s);
}

static int logger_module_init(JSContext* ctx, JSModuleDef* m) {
    JS_SetModuleExport(ctx, m, "trace",    JS_NewCFunction(ctx, logger_trace,    "trace",    1));
    JS_SetModuleExport(ctx, m, "debug",    JS_NewCFunction(ctx, logger_debug,    "debug",    1));
    JS_SetModuleExport(ctx, m, "info",     JS_NewCFunction(ctx, logger_info,     "info",     1));
    JS_SetModuleExport(ctx, m, "warn",     JS_NewCFunction(ctx, logger_warn,     "warn",     1));
    JS_SetModuleExport(ctx, m, "error",    JS_NewCFunction(ctx, logger_error,    "error",    1));
    JS_SetModuleExport(ctx, m, "critical", JS_NewCFunction(ctx, logger_critical, "critical", 1));
    JS_SetModuleExport(ctx, m, "setLevel", JS_NewCFunction(ctx, logger_setLevel, "setLevel", 1));
    JS_SetModuleExport(ctx, m, "getLevel", JS_NewCFunction(ctx, logger_getLevel, "getLevel", 0));
    return 0;
}

} // anonymous namespace

namespace noix::script {

void registerLoggerModule(JSContext* ctx) {
    JSModuleDef* m = JS_NewCModule(ctx, "noix:logger", logger_module_init);
    if (!m) {
        Logger::instance().error("NativeModules: failed to register noix:logger");
        return;
    }
    JS_AddModuleExport(ctx, m, "trace");
    JS_AddModuleExport(ctx, m, "debug");
    JS_AddModuleExport(ctx, m, "info");
    JS_AddModuleExport(ctx, m, "warn");
    JS_AddModuleExport(ctx, m, "error");
    JS_AddModuleExport(ctx, m, "critical");
    JS_AddModuleExport(ctx, m, "setLevel");
    JS_AddModuleExport(ctx, m, "getLevel");
}

} // namespace noix::script
