#pragma once

/*
 * DapBridge — DAP debug bridge for QuickJS, integrated with the noix engine.
 *
 * Uses member state instead of globals, and holds a back-pointer to
 * ScriptEngine for game-loop freeze/resume integration.
 *
 * Transport: TCP (DapSocket) — for VS Code attach mode (DebugAdapterServer)
 *
 * Thread model:
 *   Reader thread  — reads DAP messages from transport, enqueues requests
 *   Handler thread — dispatches DAP requests, enqueues script ops
 *   Script thread  — owned by ScriptEngine, executes JS + debug callbacks
 */

#include "debug/DapObjectRefStore.h"
#include "debug/DapSocket.h"
#include "debug/SourceMap.h"
#include "quickjs.h"

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

namespace noix::script { class ScriptEngine; }

namespace noix::debug {

/* ---- Scope varRef encoding ---- */

static const int SCOPE_REF_LOCAL    = 1;
static const int SCOPE_REF_CLOSURE  = 2;
static const int SCOPE_REF_GLOBAL   = 3;

/* ---- DapBridge class ---- */

class DapBridge {
public:
    DapBridge();
    ~DapBridge();

    DapBridge(const DapBridge&) = delete;
    DapBridge& operator=(const DapBridge&) = delete;

    /* Called by DapServer to set up connection to ScriptEngine */
    void setEngine(script::ScriptEngine* engine) { _engine = engine; }
    void setDebugEventTypes(uint32_t freezeType, uint32_t resumeType);

    /* Start/stop the handler thread */
    void startHandlerThread();
    void stopHandlerThread();

    /* Shutdown flag (accessible from transport code) */
    std::atomic<bool> shuttingDown{false};

    /* Resume the game loop (used by DapServer::stop to unfreeze on exit) */
    void resumeGameLoop() { pushResumeEvent(); }

    /* Close client socket to unblock the reader thread.
       Must hold socket.writeMutex or be in a context where no other
       thread is writing (e.g., during shutdown after closing the socket). */
    void closeTransport();

    /* Socket — thread-safe TCP wrapper for DAP transport */
    DapSocket socket;

    /* Reset session state for a new client connection (keeps server socket alive) */
    void resetSession();

    /* Handle client disconnection: resume script if paused, remove breakpoints,
       reset session state. Safe to call multiple times (idempotent).
       Called from handleDisconnect (handler thread) and reader thread. */
    void onClientDisconnected();

    /* QuickJS state — set by ScriptEngine after creating runtime */
    JSRuntime *rt = nullptr;
    JSContext *ctx = nullptr;

    /* ---- DAP protocol methods ---- */

    void pushEvent(const std::string &eventType, cJSON *body);
    void sendResponse(int requestSeq, const char *command, bool success,
                      const char *message, cJSON *body);
    void enqueueAndWait(std::function<void()> fn);
    void dispatchRequest(cJSON *msg);

    /* Static QuickJS callbacks */
    static void drainQueue(void *opaque);
    static void debugCallback(JSRuntime *rt, JSDebugEventType event,
                              const char *filename, int line, int col,
                              uint32_t bp_id, void *opaque);

    /* ---- DAP request handlers ---- */

    void handleInitialize(cJSON *args, int requestSeq);
    void handleLaunch(cJSON *args, int requestSeq, const char *commandName = "launch");
    void handleDisconnect(int requestSeq);
    void handleSetBreakpoints(cJSON *args, int requestSeq);
    void handleSetExceptionBreakpoints(cJSON *args, int requestSeq);
    void handleContinue(int requestSeq);
    void handleNext(int requestSeq);
    void handleStepIn(int requestSeq);
    void handleStepOut(int requestSeq);
    void handlePause(int requestSeq);
    void handleStackTrace(cJSON *args, int requestSeq);
    void handleScopes(cJSON *args, int requestSeq);
    void handleVariables(cJSON *args, int requestSeq);
    void handleEvaluate(cJSON *args, int requestSeq);
    void handleThreads(int requestSeq);
    void handleSource(cJSON *args, int requestSeq);
    void handleLoadedSources(int requestSeq);

    /* ---- State ---- */

    int seq = 1;
    bool initialized = false;
    bool launched = false;
    std::atomic<bool> running{false};
    std::atomic<bool> configDone{false};
    std::string scriptPath;
    bool stopOnEntry = false;
    std::atomic<bool> firstStop{true}; /* first stop = "entry" reason per DAP convention */

    /* Pending stopped event (buffered until configurationDone) */
    struct PendingStop {
        std::string reason;
        int threadId = 0;
        int line = 0;
        int column = 0;
        unsigned int bp_id = 0;
        bool valid = false;
    };
    PendingStop pendingStop;

    /* Handler thread */
    std::thread handlerThread;

    /* Command queue: handler thread -> script thread */
    std::mutex cmdMutex;
    std::condition_variable cmdCv;
    std::queue<std::function<void()>> cmdQueue;

    /* Request queue: reader thread -> handler thread */
    std::mutex reqMutex;
    std::condition_variable reqCv;
    std::queue<std::string> reqQueue;

    /* ---- Breakpoint tracking ---- */

    struct Breakpoint {
        unsigned int id;
        std::string filename;
        int line;
        std::string condition;
        bool verified;
    };
    std::vector<Breakpoint> breakpoints;

    struct PendingBreakpoint {
        std::string filename;
        int line;
        std::string condition;
    };
    std::vector<PendingBreakpoint> pendingBreakpoints;

    int pendingExceptionState = 0;

    /* ---- Object expansion (variablesReference for JS objects) ---- */

    ObjectRefStore _objectRefs;

    ObjectRefStore &objectRefs() { return _objectRefs; }

    /* ---- SourceMap support ---- */

    /* Cache: JS absolute path → parsed SourceMap */
    std::unordered_map<std::string, SourceMap> _sourceMapCache;

    /* sourceReference ID counter for original sources not on disk */
    int _nextSourceRef = 1;

    /* sourceReference → original source absolute path */
    std::unordered_map<int, std::string> _sourceRefPaths;

    /* Get or parse the SourceMap for a JS file (cached) */
    SourceMap &getSourceMap(const std::string &jsAbsPath);

    /* Pre-warm source map cache for all scripts loaded by QuickJS.
       Must be called on the script thread (inside enqueueAndWait). */
    void warmSourceMapCache();

    /* Translate QuickJS .js path/line to original TS path/line */
    std::string resolveOriginalSource(const std::string &jsPath, int jsLine,
                                       int &outOrigLine, int &outOrigCol);

    /* Resolve a client TS path + line to QuickJS internal JS path + line
       by walking loaded scripts and their source maps.
       Must be called on the script thread (inside enqueueAndWait).
       Returns the resolved JS path; sets outJsLine.
       If no mapping found, returns tsPath with outJsLine = tsLine. */
    std::string resolveBreakpointPath(const std::string &tsPath, int tsLine,
                                      int &outJsLine);

    /* Execute the script on the calling (script) thread.
       Called via ScriptEngine::postTask from handleLaunch.
       Does: read file, apply pending breakpoints, eval, run pending jobs,
       send terminated event. */
    void executeScript();

private:
    script::ScriptEngine* _engine = nullptr;
    uint32_t _freezeEventType = 0;
    uint32_t _resumeEventType = 0;

    void pushFreezeEvent();
    void pushResumeEvent();
    void handlerThreadFunc();
};

/* ---- Transport initialization functions ---- */

} // namespace noix::debug
