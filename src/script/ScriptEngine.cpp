#include "script/ScriptEngine.h"
#include "debug/DapBridge.h"
#include "core/Logger.h"
#include "script/NativeModules.h"

#include "quickjs.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace noix::script {

/* QuickJS interrupt handler: checks _running flag to allow forced termination
   of scripts with infinite loops when the engine is shutting down. */
static int jsInterruptHandler(JSRuntime *rt, void *opaque) {
    auto *engine = static_cast<ScriptEngine *>(opaque);
    return engine->isRunning() ? 0 : 1;
}

/* Module loader: reads JS source files for import statements.
   Only handles file-based modules; noix: modules are C-native and resolved before this. */
static JSModuleDef* moduleLoader(JSContext* ctx, const char* module_name, void* opaque) {
    auto* engine = static_cast<ScriptEngine*>(opaque);
    std::string path(module_name);

    /* Relative imports: resolve against scriptsPath */
    if (path.starts_with("./") || path.starts_with("../")) {
        path = engine->scriptsPath() + "/" + path;
    }

    /* Read file */
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        JS_ThrowReferenceError(ctx, "could not load module '%s'", module_name);
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string buf(len, '\0');
    fread(buf.data(), 1, len, f);
    fclose(f);

    /* Compile and instantiate the module.
       Only add JS_EVAL_FLAG_DEBUG_INFO when DAP bridge is present. */
    int evalFlags = JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY;
    if (engine->dapBridge()) evalFlags |= JS_EVAL_FLAG_DEBUG_INFO;
    JSValue func = JS_Eval(ctx, buf.c_str(), buf.size(), module_name, evalFlags);
    if (JS_IsException(func)) {
        return nullptr;
    }

    JSModuleDef* m = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(func));
    return m;
}

ScriptEngine::ScriptEngine(const std::string& basePath)
    : _scriptsPath(basePath) {
    /* Normalize path separators to '/' and ensure no trailing slash */
    std::replace(_scriptsPath.begin(), _scriptsPath.end(), '\\', '/');
    if (!_scriptsPath.empty() && _scriptsPath.back() == '/') {
        _scriptsPath.pop_back();
    }
    _scriptsPath += "/scripts";
}

ScriptEngine::~ScriptEngine() {
    stop();
}

void ScriptEngine::start() {
    if (_running.load()) return;
    _running.store(true);
    _thread = std::thread(&ScriptEngine::scriptThreadFunc, this);
    core::Logger::instance().info("ScriptEngine started");
}

void ScriptEngine::stop() {
    if (!_running.load()) return;
    core::Logger::instance().info("ScriptEngine::stop: begin");
    _running.store(false);
    {
        std::lock_guard lock(_queueMutex);
        _queueCv.notify_one();
    }
    core::Logger::instance().info("ScriptEngine::stop: joining script thread...");
    if (_thread.joinable()) {
        _thread.join();
    }
    core::Logger::instance().info("ScriptEngine stopped");
}

void ScriptEngine::postTask(std::function<void()> task) {
    {
        std::lock_guard lock(_queueMutex);
        _taskQueue.push(std::move(task));
    }
    _queueCv.notify_one();
}

void ScriptEngine::drainTaskQueue() {
    while (true) {
        std::function<void()> task;
        {
            std::lock_guard lock(_queueMutex);
            if (_taskQueue.empty()) break;
            task = std::move(_taskQueue.front());
            _taskQueue.pop();
        }
        if (task) {
            task();
        }
    }
}

void ScriptEngine::setDapBridge(debug::DapBridge* bridge) {
    _dapBridge = bridge;
}

void ScriptEngine::setDebugEventTypes(uint32_t freezeType, uint32_t resumeType) {
    _freezeEventType = freezeType;
    _resumeEventType = resumeType;
}

void ScriptEngine::scriptThreadFunc() {
    /* Create QuickJS runtime and context */
    _rt = JS_NewRuntime();
    _ctx = JS_NewContext(_rt);

    if (!_rt || !_ctx) {
        core::Logger::instance().error("ScriptEngine: failed to create QuickJS runtime");
        return;
    }

    /* Set interrupt handler so scripts with infinite loops can be terminated
       when the engine shuts down (_running becomes false). */
    JS_SetInterruptHandler(_rt, jsInterruptHandler, this);

    /* Wire up DAP bridge if present */
    if (_dapBridge) {
        _dapBridge->rt = _rt;
        _dapBridge->ctx = _ctx;

        JS_SetDebugCallback(_rt, debug::DapBridge::debugCallback, _dapBridge);
        JS_SetDebugDrainQueue(_rt, debug::DapBridge::drainQueue);

        _dapBridge->setDebugEventTypes(_freezeEventType, _resumeEventType);
    }

    core::Logger::instance().info("ScriptEngine: QuickJS runtime initialized");

    /* Register native modules (noix:logger, etc.) */
    registerNativeModules(_ctx);

    /* Set up module loader for file-based JS imports */
    JS_SetModuleLoaderFunc(_rt, nullptr, moduleLoader, this);

    /* Load entry script */
    std::string entryPath = _scriptsPath + "/entry.js";
    if (!loadScript(entryPath)) {
        core::Logger::instance().warn("ScriptEngine: entry script not found: {}", entryPath);
    }

    /* Job loop: keep thread alive, process tasks on demand */
    while (_running.load()) {
        std::function<void()> task;
        {
            std::unique_lock lock(_queueMutex);
            _queueCv.wait(lock, [this] { return !_taskQueue.empty() || !_running.load(); });
            if (!_taskQueue.empty()) {
                task = std::move(_taskQueue.front());
                _taskQueue.pop();
            }
        }
        if (task) {
            task();
        }
    }

    /* Cleanup QuickJS */
    if (_dapBridge) {
        _dapBridge->rt = nullptr;
        _dapBridge->ctx = nullptr;
    }
    if (_ctx) { JS_FreeContext(_ctx); _ctx = nullptr; }
    if (_rt) { JS_RunGC(_rt); JS_FreeRuntime(_rt); _rt = nullptr; }
}

bool ScriptEngine::loadScript(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string buf(len, '\0');
    fread(buf.data(), 1, len, f);
    fclose(f);

    /* Only enable debug info when DAP bridge is present. Without DAP,
       JS_EVAL_FLAG_DEBUG_INFO can cause the script to pause on debugger
       statements with no way to resume — leading to a deadlock. */
    int evalFlags = JS_EVAL_TYPE_MODULE;
    if (_dapBridge) evalFlags |= JS_EVAL_FLAG_DEBUG_INFO;

    JSValue result = JS_Eval(_ctx, buf.c_str(), buf.size(), path.c_str(), evalFlags);
    if (JS_IsException(result)) {
        JSValue exc = JS_GetException(_ctx);
        const char* msg = JS_ToCString(_ctx, exc);
        core::Logger::instance().error("ScriptEngine: error loading '{}': {}",
                                        path, msg ? msg : "unknown");
        JS_FreeCString(_ctx, msg);
        JS_FreeValue(_ctx, exc);
        return false;
    }
    JS_FreeValue(_ctx, result);
    core::Logger::instance().info("ScriptEngine: loaded '{}'", path);
    return true;
}

} // namespace noix::script
