#pragma once

/*
 * DapBridge — DAP debug bridge for QuickJS, integrated with the noix engine.
 *
 * Extracted from DapTestBridge.cpp into a proper class in noix::debug.
 * Uses member state instead of globals, and holds a back-pointer to
 * ScriptEngine for game-loop freeze/resume integration.
 *
 * Transport modes:
 *   stdio (default)  —  for VS Code launch mode (DebugAdapterExecutable)
 *   TCP (--port N)   —  for VS Code attach mode (DebugAdapterServer)
 *
 * Thread model:
 *   Reader thread  — reads DAP messages from transport, enqueues requests
 *   Handler thread — dispatches DAP requests, enqueues script ops
 *   Script thread  — owned by ScriptEngine, executes JS + debug callbacks
 */

#include "quickjs.h"

#include <SDL3_net/SDL_net.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

struct cJSON;

namespace noix::script { class ScriptEngine; }

namespace noix::debug {

/* ---- Transport abstraction for DAP wire protocol ---- */

struct DapTransport {
    /* Read a single byte. Returns -1 on EOF/error. */
    int (*readByte)(void *ctx);
    /* Write a complete DAP message (Content-Length header + JSON body). */
    void (*writeMessage)(void *ctx, const std::string &msg);
    void *ctx = nullptr;
};

/* ---- TCP context for SDL_net-based transport ---- */

struct TcpCtx {
    NET_Server *server = nullptr;
    NET_StreamSocket *client = nullptr;
    std::string recvBuffer;
    int port = 0;
    std::atomic<bool> *shuttingDown = nullptr; /* pointer to DapBridge::shuttingDown */
};

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

    /* Transport initialization (called by DapServer) */
    void setTransport(const DapTransport& transport);

    /* Shutdown flag (accessible from transport code) */
    std::atomic<bool> shuttingDown{false};

    /* Resume the game loop (used by DapServer::stop to unfreeze on exit) */
    void resumeGameLoop() { pushResumeEvent(); }

    /* Close transport (unblocks the reader thread) */
    void closeTransport();

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

    /* Transport */
    DapTransport transport;
    std::mutex writeMutex;

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

    struct ObjectRef {
        int varRef;         /* the variablesReference that refers to this object */
        JSValue obj;        /* the JS object value */
    };
    std::vector<ObjectRef> objectRefs;
    int nextObjectVarRef = 100000; /* start high to avoid collision with scope refs */

    int addObjectRef(JSValue obj);
    JSValue findObjectRef(int varRef);
    void clearObjectRefs();

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

void init_stdio_transport(DapTransport *t);
bool init_tcp_transport(DapTransport *t, TcpCtx *tcp, int port,
                         std::atomic<bool> &shuttingDown);
void cleanup_tcp(TcpCtx *tcp);

/* ---- DAP wire protocol ---- */

bool dap_read_message(DapTransport &transport, std::string &out);
void dap_write_message(DapTransport &transport, std::mutex &writeMutex,
                        const std::string &json);

} // namespace noix::debug
