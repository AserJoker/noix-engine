#include "script/JSEngine.h"
#include <quickjs.h>
#include <SDL3/SDL.h>

namespace noix::script {

JSEngine::JSEngine()
    : _rt(JS_NewRuntime()), _ctx(JS_NewContext(_rt)) {}

JSEngine::~JSEngine() {
    JS_FreeContext(_ctx);
    JS_FreeRuntime(_rt);
}

std::string JSEngine::eval(const std::string& code, const std::string& filename) {
    JSValue val = JS_Eval(_ctx, code.c_str(), code.size(),
                          filename.c_str(), JS_EVAL_TYPE_GLOBAL);

    if (JS_IsException(val)) {
        JSValue err = JS_GetException(_ctx);
        const char* msg = JS_ToCString(_ctx, err);
        std::string error = msg ? msg : "unknown error";
        JS_FreeCString(_ctx, msg);
        JS_FreeValue(_ctx, err);
        JS_FreeValue(_ctx, val);
        return error;
    }

    const char* str = JS_ToCString(_ctx, val);
    std::string result = str ? str : "";
    JS_FreeCString(_ctx, str);
    JS_FreeValue(_ctx, val);
    return result;
}

} // namespace noix::script
