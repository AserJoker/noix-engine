#pragma once

struct JSContext;

namespace noix::script {

class ScriptEngine;

/// Register all native C modules into the QuickJS context (called on script thread)
void registerNativeModules(JSContext* ctx, ScriptEngine* engine);

/// Individual module registration (called by registerNativeModules)
void registerLoggerModule(JSContext* ctx);
void registerDebugModule(JSContext* ctx);

} // namespace noix::script
