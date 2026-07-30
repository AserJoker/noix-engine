#include "script/NativeModules.h"
#include "core/Logger.h"
#include "quickjs.h"

namespace noix::script {

void registerNativeModules(JSContext* ctx, ScriptEngine* engine) {
    /* Store engine in runtime opaque so C callbacks can access it */
    JS_SetRuntimeOpaque(JS_GetRuntime(ctx), engine);

    registerLoggerModule(ctx);
    registerDebugModule(ctx);
    registerLocaleModule(ctx);
    registerConfigModule(ctx);
}

} // namespace noix::script
