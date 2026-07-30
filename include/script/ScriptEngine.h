#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

struct JSRuntime;
struct JSContext;
struct JSValue;
struct JSModuleDef;

namespace noix::core {
    class Value;
}

namespace noix::debug {
    class DapBridge;
    class DebugServer;
}

namespace noix::runtime { class EventBus; }

namespace noix::script {

class ScriptEngine {
public:
    ScriptEngine(const std::string& basePath);
    ~ScriptEngine();

    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    /// Start the script thread
    void start();

    /// Gracefully stop the script thread
    void stop();

    /// Tear down and reinitialize QuickJS runtime/context (hot-reload support).
    /// Must be called from outside the script thread (dispatches work internally).
    void reset();

    /// Post a task to the script thread (thread-safe)
    void postTask(std::function<void()> task);

    /// Drain the task queue (called from script thread during debug pauses)
    void drainTaskQueue();

    /// Get the scripts directory path
    const std::string& scriptsPath() const { return _scriptsPath; }

    /// Set DAP bridge (call before start)
    void setDapBridge(debug::DapBridge* bridge);

    /// Get DAP bridge
    debug::DapBridge* dapBridge() const { return _dapBridge; }

    /// Set DebugServer back-pointer (call before start)
    void setDebugServer(debug::DebugServer* server) { _debugServer = server; }

    /// Get DebugServer
    debug::DebugServer* debugServer() const { return _debugServer; }

    /// Set EventBus back-pointer (call before start)
    void setEventBus(runtime::EventBus* bus) { _eventBus = bus; }

    /// Get EventBus
    runtime::EventBus* eventBus() const { return _eventBus; }

    /// Set module resolver: maps module name to file path.
    /// Called by moduleLoader when a non-relative, non-native import is encountered.
    using ModuleResolver = std::function<std::string(const std::string& moduleName)>;
    void setModuleResolver(ModuleResolver resolver);

    /// Load and execute a script file on the script thread (for mod index execution)
    void loadScriptAsync(const std::string& path);

    /// Load and execute a script string on the script thread (for import statements)
    void loadScriptStringAsync(const std::string& code, const std::string& filename);

    /// Set debug freeze/resume SDL event types
    void setDebugEventTypes(uint32_t freezeType, uint32_t resumeType);

    /// Check if script thread is running (for interrupt handler)
    bool isRunning() const { return _running.load(); }

    /// QuickJS runtime/context access (script thread only)
    JSRuntime* runtime() const { return _rt; }
    JSContext* context() const { return _ctx; }

    /// Store a named JS callback (called from script thread during module init).
    /// Duplicates the JSValue reference; caller does not need to keep it alive.
    void registerCallback(const std::string& name, JSContext* ctx, JSValue callback);

    /// Invoke a named JS callback from any thread.
    /// Posts JS_Call to script thread, waits for result, returns response Value.
    /// Returns error Value if callback not found or timed out.
    noix::core::Value invokeCallback(const std::string& name, const noix::core::Value& request);

    /// Release all stored JS callbacks on the script thread (called during reset/shutdown).
    void releaseCallbacks();

private:
    void scriptThreadFunc();
    bool loadScript(const std::string& path);
    void initQuickJS();

    /// Internal callback entry
    struct CallbackEntry {
        JSContext* ctx;
        void* callback;  /* JS_VALUE_GET_PTR stored as void* */
    };

    std::string _scriptsPath;

    std::thread _thread;
    std::mutex _queueMutex;
    std::condition_variable _queueCv;
    std::queue<std::function<void()>> _taskQueue;
    std::atomic<bool> _running{false};

    /// QuickJS runtime (owned by script thread)
    JSRuntime* _rt = nullptr;
    JSContext* _ctx = nullptr;

    /// DAP bridge
    debug::DapBridge* _dapBridge = nullptr;

    /// DebugServer back-pointer
    debug::DebugServer* _debugServer = nullptr;

    /// EventBus back-pointer
    runtime::EventBus* _eventBus = nullptr;

    /// Module resolver (maps module name -> file path)
    ModuleResolver _moduleResolver;

    // Allow moduleLoader to access _moduleResolver
    friend JSModuleDef* moduleLoader(JSContext*, const char*, void*);

    /// Named JS callbacks (owned by ScriptEngine, guarded by _callbacksMutex)
    std::map<std::string, CallbackEntry> _callbacks;
    std::mutex _callbacksMutex;

    // Debug event types (set before start)
    uint32_t _freezeEventType{0};
    uint32_t _resumeEventType{0};
};

} // namespace noix::script
