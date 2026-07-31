#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "script/ScriptEngine.h"
#include "debug/DapBridge.h"
#include "debug/DebugServer.h"
#include "debug/commands/ScriptCallbackCommand.h"
#include "core/Logger.h"
#include "core/Value.h"
#include "runtime/EventBus.h"
#include "script/NativeModules.h"

#include "quickjs.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>

namespace noix::script {

/* QuickJS interrupt handler: checks _running flag to allow forced termination
   of scripts with infinite loops when the engine is shutting down. */
static int jsInterruptHandler(JSRuntime *rt, void *opaque) {
    auto *engine = static_cast<ScriptEngine *>(opaque);
    return engine->isRunning() ? 0 : 1;
}

/* Module normalize: resolves module names to absolute paths.
   - Relative imports (./ ../): resolve against the importing module's directory
   - Bare names: resolved via ModuleResolver callback (e.g. mod names → absolute paths)
   - Falls back to the bare name as-is (for native modules like noix:logger)
   The returned string is allocated with JS_Malloc; caller frees with js_free. */
static char* moduleNormalize(JSContext* ctx, const char* base_cname,
                             const char* cname1, void* opaque) {
    auto* engine = static_cast<ScriptEngine*>(opaque);
    std::string name(cname1);

    /* Relative imports: resolve against base module's directory */
    if (name.starts_with("./") || name.starts_with("../")) {
        std::filesystem::path basePath(base_cname);
        auto resolved = (basePath.parent_path() / name).lexically_normal();
        std::string result = resolved.string();
        std::replace(result.begin(), result.end(), '\\', '/');
        char* cstr = static_cast<char*>(js_malloc(ctx, result.size() + 1));
        if (!cstr) return nullptr;
        memcpy(cstr, result.c_str(), result.size() + 1);
        return cstr;
    }

    /* Try ModuleResolver for bare module names (e.g. mod names) */
    if (engine->_moduleResolver) {
        std::string resolved = engine->_moduleResolver(name);
        if (!resolved.empty()) {
            std::replace(resolved.begin(), resolved.end(), '\\', '/');
            char* cstr = static_cast<char*>(js_malloc(ctx, resolved.size() + 1));
            if (!cstr) return nullptr;
            memcpy(cstr, resolved.c_str(), resolved.size() + 1);
            return cstr;
        }
    }

    /* Fallback: bare name as-is (for native modules like noix:logger) */
    char* cstr = static_cast<char*>(js_malloc(ctx, name.size() + 1));
    if (!cstr) return nullptr;
    memcpy(cstr, name.c_str(), name.size() + 1);
    return cstr;
}

/* Module loader: loads and compiles a module file.
   module_name is already normalized (absolute path for file modules,
   bare name for native modules which are handled before this callback). */
static JSModuleDef* moduleLoader(JSContext* ctx, const char* module_name, void* opaque) {
    auto* engine = static_cast<ScriptEngine*>(opaque);
    std::string path(module_name);

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

    /* Compile the module. Use the normalized name (absolute path) as filename
       so QuickJS caches the module under its absolute path. */
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
    core::Logger::instance().debug("ScriptEngine::stop: begin");
    _running.store(false);
    {
        std::lock_guard lock(_queueMutex);
        _queueCv.notify_one();
    }
    core::Logger::instance().debug("ScriptEngine::stop: joining script thread...");
    if (_thread.joinable()) {
        _thread.join();
    }
    core::Logger::instance().info("ScriptEngine stopped");
}

void ScriptEngine::reset() {
    core::Logger::instance().info("ScriptEngine::reset: dispatching reset to script thread");
    postTask([this]() {
        core::Logger::instance().debug("ScriptEngine::reset: releasing callbacks...");

        /* 1. Release all JS callback references before freeing the context */
        releaseCallbacks();

        /* 2. Tear down QuickJS */
        if (_dapBridge) {
            _dapBridge->rt = nullptr;
            _dapBridge->ctx = nullptr;
        }
        if (_ctx) { JS_FreeContext(_ctx); _ctx = nullptr; }
        if (_rt) { JS_RunGC(_rt); JS_FreeRuntime(_rt); _rt = nullptr; }

        core::Logger::instance().debug("ScriptEngine::reset: QuickJS torn down");

        /* 3. Reinitialize */
        initQuickJS();

        core::Logger::instance().debug("ScriptEngine::reset: complete");
    });
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

void ScriptEngine::registerCallback(const std::string& name, JSContext* ctx, JSValue callback) {
    JS_DupValue(ctx, callback);
    CallbackEntry entry{ctx, reinterpret_cast<void*>(JS_VALUE_GET_PTR(callback))};
    std::lock_guard lock(_callbacksMutex);
    _callbacks[name] = std::move(entry);
}

noix::core::Value ScriptEngine::invokeCallback(const std::string& name, const noix::core::Value& request) {
    CallbackEntry entry;
    {
        std::lock_guard lock(_callbacksMutex);
        auto it = _callbacks.find(name);
        if (it == _callbacks.end()) {
            return core::Value::object({{"error", core::Value("callback not found: " + name)}});
        }
        entry = it->second;
    }

    std::mutex waitMutex;
    std::condition_variable waitCv;
    bool done = false;
    core::Value result;

    postTask([&, entry]() {
        JSValue jsCallback = JS_MKPTR(JS_TAG_OBJECT, entry.callback);

        /* Convert request Value to a JS object via JSON parse */
        std::string requestJson = request.dump();
        JSValue argv[] = { JS_ParseJSON(entry.ctx, requestJson.c_str(), requestJson.size(), "<request>") };
        JSValue jsResult = JS_Call(entry.ctx, jsCallback, JS_UNDEFINED, 1, argv);
        JS_FreeValue(entry.ctx, argv[0]);

        /* Convert JS result back to Value.
           Accepts both objects (JSON.stringify internally) and strings. */
        core::Value responseValue;
        if (JS_IsException(jsResult)) {
            JSValue exc = JS_GetException(entry.ctx);
            const char* err = JS_ToCString(entry.ctx, exc);
            responseValue = core::Value::object({{"error", core::Value(err ? err : "script exception")}});
            if (err) JS_FreeCString(entry.ctx, err);
            JS_FreeValue(entry.ctx, exc);
        } else if (JS_IsString(jsResult)) {
            const char* s = JS_ToCString(entry.ctx, jsResult);
            if (s) {
                responseValue = core::Value::parse(std::string(s));
                JS_FreeCString(entry.ctx, s);
            }
            if (responseValue.isNull()) {
                responseValue = core::Value::object();
            }
        } else if (!JS_IsUndefined(jsResult) && !JS_IsNull(jsResult)) {
            /* Object/array/number/bool — stringify then parse to Value */
            JSValue jsonStr = JS_JSONStringify(entry.ctx, jsResult, JS_UNDEFINED, JS_UNDEFINED);
            if (JS_IsString(jsonStr)) {
                const char* s = JS_ToCString(entry.ctx, jsonStr);
                if (s) {
                    responseValue = core::Value::parse(std::string(s));
                    JS_FreeCString(entry.ctx, s);
                }
            }
            JS_FreeValue(entry.ctx, jsonStr);
            if (responseValue.isNull()) {
                responseValue = core::Value::object();
            }
        } else {
            /* undefined/null return — empty object */
            responseValue = core::Value::object();
        }
        JS_FreeValue(entry.ctx, jsResult);

        {
            std::lock_guard<std::mutex> lk(waitMutex);
            result = std::move(responseValue);
            done = true;
        }
        waitCv.notify_one();
    });

    /* Wait for script thread to finish (with timeout for shutdown safety) */
    std::unique_lock<std::mutex> lk(waitMutex);
    while (!done) {
        if (waitCv.wait_for(lk, std::chrono::milliseconds(5000)) == std::cv_status::timeout) {
            return core::Value::object({{"error", core::Value("script callback timed out")}});
        }
    }

    return result;
}

void ScriptEngine::releaseCallbacks() {
    /* Release EventBus listeners first (same thread: script thread) */
    if (_eventBus) {
        _eventBus->releaseAllListeners();
    }

    std::map<std::string, CallbackEntry> callbacks;
    {
        std::lock_guard lock(_callbacksMutex);
        callbacks = std::move(_callbacks);
    }

    /* Must free JSValues on the script thread */
    for (auto& [name, entry] : callbacks) {
        auto* cb = entry.callback;
        auto* ctx = entry.ctx;
        postTask([ctx, cb]() {
            JSValue val = JS_MKPTR(JS_TAG_OBJECT, cb);
            JS_FreeValue(ctx, val);
        });
    }
}

void ScriptEngine::setModuleResolver(ModuleResolver resolver) {
    _moduleResolver = std::move(resolver);
}

void ScriptEngine::loadScriptAsync(const std::string& path) {
    postTask([this, path]() {
        if (!loadScript(path)) {
            core::Logger::instance().warn("ScriptEngine: failed to load script: {}", path);
        }
    });
}

void ScriptEngine::initQuickJS() {
    _rt = JS_NewRuntime();
    _ctx = JS_NewContext(_rt);

    if (!_rt || !_ctx) {
        core::Logger::instance().error("ScriptEngine: failed to create QuickJS runtime");
        return;
    }

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

    /* Register native modules */
    registerNativeModules(_ctx, this);

    /* Set up module loader with custom normalize function */
    JS_SetModuleLoaderFunc(_rt, moduleNormalize, moduleLoader, this);
}

void ScriptEngine::scriptThreadFunc() {
    initQuickJS();

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

    /* Cleanup QuickJS — release all JS callbacks before freeing context */
    releaseCallbacks();

    /* Drain any pending releaseCallback tasks */
    drainTaskQueue();

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

    /* Normalize path to forward slashes for consistent QuickJS module caching */
    std::string normalizedPath = path;
    std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');

    /* Only enable debug info when DAP bridge is present. Without DAP,
       JS_EVAL_FLAG_DEBUG_INFO can cause the script to pause on debugger
       statements with no way to resume — leading to a deadlock. */
    int evalFlags = JS_EVAL_TYPE_MODULE;
    if (_dapBridge) evalFlags |= JS_EVAL_FLAG_DEBUG_INFO;

    JSValue result = JS_Eval(_ctx, buf.c_str(), buf.size(), normalizedPath.c_str(), evalFlags);
    if (JS_IsException(result)) {
        JSValue exc = JS_GetException(_ctx);
        const char* msg = JS_ToCString(_ctx, exc);
        core::Logger::instance().error("ScriptEngine: error loading '{}': {}",
                                        normalizedPath, msg ? msg : "unknown");
        JS_FreeCString(_ctx, msg);
        JS_FreeValue(_ctx, exc);
        return false;
    }
    JS_FreeValue(_ctx, result);
    core::Logger::instance().info("ScriptEngine: loaded '{}'", normalizedPath);
    return true;
}

} // namespace noix::script
