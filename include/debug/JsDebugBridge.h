#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct cJSON;
#include <quickjs.h>

namespace noix::debug {

/// Bridges CDP commands to QuickJS debug API.
/// Owns JSRuntime/JSContext and runs a script thread.
/// Commands that call QuickJS APIs (evaluate, getPossibleBreakpoints) are
/// forwarded to the script thread via a command queue. Other commands that
/// only touch shared data structures run directly on the WS thread.
class JsDebugBridge {
public:
    JsDebugBridge();
    ~JsDebugBridge();

    JsDebugBridge(const JsDebugBridge&) = delete;
    JsDebugBridge& operator=(const JsDebugBridge&) = delete;

    /// Load and run a script file. Returns false on error.
    /// If debugWait is true, wait for debugger attach before evaluating.
    bool start(const std::string& scriptPath, bool debugWait = false);
    void stop();

    // CDP command handlers (called from WS thread).
    // Return cJSON* result (caller takes ownership).
    cJSON* debuggerEnable(const cJSON* params);
    cJSON* debuggerDisable();
    cJSON* debuggerPause();
    cJSON* debuggerResume();
    cJSON* debuggerStepInto();
    cJSON* debuggerStepOver();
    cJSON* debuggerStepOut();
    cJSON* debuggerSetBreakpoint(const cJSON* params);
    cJSON* debuggerSetBreakpointByUrl(const cJSON* params);
    cJSON* debuggerRemoveBreakpoint(const cJSON* params);
    cJSON* debuggerSetBreakpointsActive(const cJSON* params);
    cJSON* debuggerGetPossibleBreakpoints(const cJSON* params);
    cJSON* debuggerEvaluateOnCallFrame(const cJSON* params);
    cJSON* debuggerGetScriptSource(const cJSON* params);
    cJSON* runtimeEnable();
    cJSON* runtimeEvaluate(const cJSON* params);
    cJSON* runtimeCallFunctionOn(const cJSON* params);

    using EventHandler = std::function<void(const std::string& method, cJSON* params)>;
    void setEventHandler(EventHandler handler);

    /// Called when Chrome sends Runtime.runIfWaitingForDebugger
    void notifyDebuggerReady() { _debuggerReady.store(true); }

    /// Poll for pending CDP events. Returns (method, params) pairs.
    /// Caller takes ownership of each cJSON* params.
    std::vector<std::pair<std::string, cJSON*>> pollEvents();

private:
    /// Command that runs on the script thread and returns a result.
    struct Command {
        std::function<cJSON*()> execute;  // runs on script thread
        cJSON* result = nullptr;          // output (bridge owns until collected)
        std::mutex doneMutex;
        std::condition_variable doneCv;
        bool done = false;

        void signalDone() {
            std::lock_guard lock(doneMutex);
            done = true;
            doneCv.notify_one();
        }

        /// Wait with timeout. Returns true if completed, false on timeout.
        bool waitFor(std::chrono::milliseconds timeout) {
            std::unique_lock lock(doneMutex);
            return doneCv.wait_for(lock, timeout, [this] { return done; });
        }
    };

    struct Event {
        std::string method;
        cJSON* params;  // owned, caller must cJSON_Delete
    };

    static void debugCallback(JSRuntime* rt, JSDebugEventType event,
                               const char* filename,
                               int line, int col, uint32_t bp_id, void* opaque);
    static void drainQueue(void* opaque);

    void scriptThreadFunc();
    void processCommands();
    void pushEvent(const std::string& method, cJSON* params);

    /// Enqueue a command to the script thread and wait for result.
    /// Returns the result, or empty object on timeout.
    cJSON* enqueueAndWait(std::function<cJSON*()> fn);

    cJSON* jsValueToRemoteObject(JSContext* ctx, JSValue val);
    cJSON* buildCallFrames();

    JSRuntime* _rt = nullptr;
    JSContext* _ctx = nullptr;
    std::thread _scriptThread;
    std::atomic<bool> _running{false};

    // Command queue (WS -> Script)
    std::mutex _cmdMutex;
    std::queue<Command*> _cmdQueue;

    // Event queue (Script -> WS)
    std::mutex _evtMutex;
    std::vector<Event> _evtQueue;

    // ID mappings
    uint32_t _nextBpId = 1;
    std::unordered_map<std::string, uint32_t> _cdpToQjs;   // CDP bp string -> QJS uint32
    std::unordered_map<uint32_t, std::string> _qjsToCdp;   // QJS uint32 -> CDP bp string
    uint32_t _nextScriptId = 1;
    std::unordered_map<std::string, std::string> _filenameToId;  // filename -> scriptId
    std::unordered_map<std::string, std::string> _idToFilename;  // scriptId -> filename
    std::unordered_map<std::string, std::string> _scriptIdToSource; // scriptId -> source code
    std::unordered_map<std::string, std::string> _urlToFilename; // URL (file:///...) -> filename

    // State flags
    bool _debuggerEnabled = false;
    bool _runtimeEnabled = false;
    bool _debugWait = false;
    std::atomic<bool> _scriptEvaluated{false};
    std::atomic<bool> _debuggerReady{false};  // set by Runtime.runIfWaitingForDebugger
    std::string _scriptPath;

    // Skip location: after resume/step, skip re-hitting the same line
    std::string _skipFilename;
    int _skipLine = -1;
    int _lastStepKind = -1;  // -1=continue, 0=stepInto, 1=stepOver, 2=stepOut
};

} // namespace noix::debug
