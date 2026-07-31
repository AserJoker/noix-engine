/*
 * DapBridgeCore — Lifecycle, threading, and communication for DapBridge.
 *
 * Contains constructor/destructor, transport management, session reset,
 * event/response sending, enqueueAndWait, and the handler/drain/debug callbacks.
 */

#include "debug/DapBridge.h"
#include "DapBridgeUtils.h"
#include "script/ScriptEngine.h"
#include "cJSON.h"
#include "core/Logger.h"

#include <SDL3/SDL.h>

namespace noix::debug {

/* ---- Constructor / Destructor ---- */

DapBridge::DapBridge() = default;

DapBridge::~DapBridge() {
    stopHandlerThread();
    /* Note: _objectRefs.clear(ctx) should ideally run on the script thread,
       but by destructor time the engine may be gone. Clear without JS_FreeValue. */
    _objectRefs.clear(nullptr);
}

/* ---- Transport management ---- */

void DapBridge::closeTransport() {
    /* Close the client socket so the reader thread's blocking read unblocks.
       DapSocket::closeClient() sets _closed=true first, then destroys
       the socket — this makes all subsequent I/O return immediately.

       Caller must hold socket.writeMutex (or be in a context where no
       other thread is writing) to prevent racing with pushEvent/sendResponse. */
    socket.closeClient();
}

void DapBridge::resetSession() {
    /* Reset per-session state so a new client can connect fresh.
       Does NOT close the server socket — that's managed by DapServer.

       NOTE: We intentionally do NOT clear pendingStop here. In the
       noix-engine architecture, the script may have paused at a
       debugger statement or breakpoint BEFORE the DAP client connects.
       The debugCallback buffers the stop in pendingStop. If we clear
       it here, the client will never receive the stopped event and
       will time out waiting for it. */
    initialized = false;
    launched = false;
    running = false;
    configDone = false;
    stopOnEntry = false;
    firstStop = true;
    /* pendingStop is preserved — see note above */
    pendingBreakpoints.clear();

    /* Free JSValues on the script thread — JS_FreeValue must not be called
       from the reader thread where resetSession runs. */
    if (_engine) {
        enqueueAndWait([this]() {
            _objectRefs.clear(ctx);
        });
    } else {
        _objectRefs.clear(ctx);
    }

    _sourceMapCache.clear();
    _sourceRefPaths.clear();
    _nextSourceRef = 1;
    seq = 1;

    /* Drain any leftover requests/commands from the previous session */
    {
        std::lock_guard<std::mutex> lk(reqMutex);
        std::queue<std::string> empty;
        reqQueue.swap(empty);
    }
    {
        std::lock_guard<std::mutex> lk(cmdMutex);
        std::queue<std::function<void()>> empty;
        cmdQueue.swap(empty);
    }
}

void DapBridge::onClientDisconnected() {
    /* Called when the DAP client disconnects (either via "disconnect" request
       or by dropping the TCP connection). In attach mode, the server process
       must keep running — we only clean up the debug session.

       This method is idempotent — safe to call multiple times. */
    core::Logger::instance().debug("[DAP] onClientDisconnected: cleaning up debug session");

    /* 1. Reset session flags FIRST so debugCallback stops sending events.
       Setting launched=false prevents debugCallback from pushing freeze events
       and trying to write to the (now closed) client socket. */
    bool wasLaunched = launched;
    launched = false;
    configDone = false;
    running = false;
    stopOnEntry = false;
    pendingStop.valid = false;

    /* 2. Remove all breakpoints from QuickJS, disable debug callbacks,
       and resume the script if paused. Only do this once (when this call
       is the one that transitioned launched from true to false). */
    if (wasLaunched && rt && _engine && !shuttingDown.load()) {
        enqueueAndWait([this]() {
            /* Remove all breakpoints so the script doesn't re-pause */
            for (auto &bp : breakpoints) {
                JS_DebugRemoveBreakpoint(rt, bp.id);
            }
            breakpoints.clear();

            /* Disable debug callbacks so the script doesn't pause on
               debugger statements or exception breakpoints either.
               Will be re-enabled when a new client connects (handleLaunch). */
            JS_SetDebugCallback(rt, nullptr, nullptr);

            /* Resume the script if it's paused at a breakpoint.
               After removing breakpoints and disabling callbacks,
               it won't pause again. */
            if (JS_DebugGetState(rt) != 0) {
                core::Logger::instance().debug("[DAP] onClientDisconnected: resuming paused script");
                JS_DebugContinue(rt);
            }
        });
    } else {
        breakpoints.clear();
    }

    /* 3. Push resume event so the game loop un-freezes */
    pushResumeEvent();

    /* 4. Clear pending items — the session is over */
    pendingBreakpoints.clear();
    {
        std::lock_guard<std::mutex> lk(reqMutex);
        std::queue<std::string> empty;
        reqQueue.swap(empty);
    }
    {
        std::lock_guard<std::mutex> lk(cmdMutex);
        std::queue<std::function<void()>> empty;
        cmdQueue.swap(empty);
    }

    core::Logger::instance().debug("[DAP] onClientDisconnected: done");
}

/* ---- Core methods ---- */

void DapBridge::setDebugEventTypes(uint32_t freezeType, uint32_t resumeType) {
    _freezeEventType = freezeType;
    _resumeEventType = resumeType;
}

void DapBridge::startHandlerThread() {
    handlerThread = std::thread(&DapBridge::handlerThreadFunc, this);
}

void DapBridge::stopHandlerThread() {
    shuttingDown = true;
    reqCv.notify_one();
    if (handlerThread.joinable())
        handlerThread.join();
}

void DapBridge::pushEvent(const std::string &eventType, cJSON *body) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddNumberToObject(msg, "seq", seq++);
    cJSON_AddStringToObject(msg, "type", "event");
    cJSON_AddStringToObject(msg, "event", eventType.c_str());
    if (body) cJSON_AddItemToObject(msg, "body", body);
    char *s = cJSON_PrintUnformatted(msg);
    std::string json(s);
    cJSON_free(s);
    cJSON_Delete(msg);
    core::Logger::instance().debug("[DAP] <<< event: {} {}", eventType, json);
    /* Write event immediately -- it may be produced on the script thread
       while the handler thread is blocked waiting for a response. */
    socket.writeDapMessage(json);
}

void DapBridge::sendResponse(int requestSeq, const char *command, bool success,
                              const char *message, cJSON *body) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddNumberToObject(msg, "seq", seq++);
    cJSON_AddStringToObject(msg, "type", "response");
    cJSON_AddNumberToObject(msg, "request_seq", requestSeq);
    cJSON_AddStringToObject(msg, "command", command);
    cJSON_AddBoolToObject(msg, "success", success);
    if (message) cJSON_AddStringToObject(msg, "message", message);
    if (body) cJSON_AddItemToObject(msg, "body", body);
    char *s = cJSON_PrintUnformatted(msg);
    std::string json(s);
    cJSON_free(s);
    cJSON_Delete(msg);
    core::Logger::instance().debug("[DAP] <<< response: {}", json);
    socket.writeDapMessage(json);
}

void DapBridge::enqueueAndWait(std::function<void()> fn) {
    if (!_engine) return;

    std::mutex waitMutex;
    std::condition_variable waitCv;
    bool done = false;

    /* Post the command to the cmdQueue, which drainQueue reads from
       during debug pauses. This is the ONLY queue that gets processed
       when the script is paused at a breakpoint — the ScriptEngine's
       task loop is not running during pauses.

       Also post via postTask so the command runs when the script is
       NOT paused and the task loop is active. The shared 'done' flag
       ensures the function body executes at most once. */
    auto wrappedFn = [&, fn = std::move(fn)]() {
        if (done) return;  /* already executed via the other path */
        fn();
        {
            std::lock_guard<std::mutex> wl(waitMutex);
            done = true;
        }
        waitCv.notify_one();
    };

    {
        std::lock_guard<std::mutex> lk(cmdMutex);
        cmdQueue.push(wrappedFn);
    }
    cmdCv.notify_one();

    _engine->postTask([this, wrappedFn]() {
        /* Drain any pending cmdQueue items first — this handles the case
           where the task loop runs before drainQueue gets called. */
        std::function<void()> cmd;
        {
            std::lock_guard<std::mutex> lk(cmdMutex);
            if (!cmdQueue.empty()) {
                cmd = cmdQueue.front();
                cmdQueue.pop();
            }
        }
        if (cmd) cmd();
    });

    /* Wait for the script thread to execute the command.
       Use a timeout to avoid permanent blocking. drainQueue processes
       the cmdQueue even during shutdown, so the command will usually
       complete. But if the runtime is being destroyed (engine going away),
       we must give up after a reasonable timeout. */
    std::unique_lock<std::mutex> wl(waitMutex);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done && std::chrono::steady_clock::now() < deadline) {
        waitCv.wait_until(wl, deadline);
    }
    if (!done) {
        core::Logger::instance().warn("[DAP] enqueueAndWait: timed out waiting for command execution");
    }
}

/* ---- Debug callbacks ---- */

/* drain: called on script thread while paused */
void DapBridge::drainQueue(void *opaque) {
    auto *self = static_cast<DapBridge *>(opaque);

    /* Always process pending commands first — even during shutdown,
       because they may contain JS_DebugContinue calls from handleDisconnect.
       Skipping them would leave stale commands that get re-executed via
       postTask, causing double JS_DebugContinue. */
    std::function<void()> fn;
    {
        std::lock_guard<std::mutex> lk(self->cmdMutex);
        if (!self->cmdQueue.empty()) {
            fn = self->cmdQueue.front();
            self->cmdQueue.pop();
        }
    }
    if (fn) {
        core::Logger::instance().debug("[DAP] drainQueue: executing command, queue remaining={}",
                                        self->cmdQueue.size());
        fn();
        core::Logger::instance().debug("[DAP] drainQueue: command done, debug_state={}",
                                        self->rt ? JS_DebugGetState(self->rt) : -1);
        return; /* Command executed — the script may no longer be paused */
    }

    /* No command in the queue. If shutting down, resume the script
       so the script thread can exit. This is the safe way to continue —
       it runs on the script thread, unlike DapServer::stop() which
       runs on the main thread. */
    if (self->shuttingDown.load()) {
        core::Logger::instance().debug("[DAP] drainQueue: shuttingDown detected, calling JS_DebugContinue");
        if (self->rt) JS_DebugContinue(self->rt);
    }
}

/* debug callback: called on script thread */
void DapBridge::debugCallback(JSRuntime *rt, JSDebugEventType event,
                                const char *filename, int line, int col,
                                uint32_t bp_id, void *opaque) {
    auto *self = static_cast<DapBridge *>(opaque);

    /* If shutting down, immediately continue the script so the script
       thread can exit cleanly. */
    if (self->shuttingDown.load()) {
        core::Logger::instance().debug("[DAP] debugCallback: shuttingDown, calling JS_DebugContinue");
        JS_DebugContinue(rt);
        return;
    }

    /* If no DAP client is connected (not launched), buffer the stop event
       but do NOT freeze the game loop. The script stays paused (waiting for
       a continue from the client), but the game remains responsive so the
       user can close the window. When a client later connects and sends
       configurationDone, the buffered pendingStop is delivered. */
    if (!self->launched) {
        const char *reason = "breakpoint";
        if (event == JS_DEBUG_EVENT_EXCEPTION || event == JS_DEBUG_EVENT_UNCAUGHT_EXCEPTION)
            reason = "exception";
        else if (event == JS_DEBUG_EVENT_STEP_COMPLETE)
            reason = "step";

        self->pendingStop.reason = reason;
        self->pendingStop.threadId = 1;
        self->pendingStop.line = line;
        self->pendingStop.column = col;
        self->pendingStop.bp_id = bp_id;
        self->pendingStop.valid = true;
        /* Do NOT call pushFreezeEvent — keep game loop running */
        return;
    }

    const char *reason = "breakpoint";
    switch (event) {
    case JS_DEBUG_EVENT_BREAKPOINT_HIT:
        reason = "breakpoint";
        break;
    case JS_DEBUG_EVENT_STEP_COMPLETE:
        reason = "step";
        break;
    case JS_DEBUG_EVENT_DEBUGGER_STMT:
        reason = "breakpoint";
        break;
    case JS_DEBUG_EVENT_EXCEPTION:
    case JS_DEBUG_EVENT_UNCAUGHT_EXCEPTION:
        reason = "exception";
        break;
    }

    /* DAP convention: stop-on-entry uses reason "entry".
       Only override the reason when stopOnEntry is set. */
    if (self->stopOnEntry) {
        reason = "entry";
        self->stopOnEntry = false;
    }

    core::Logger::instance().debug(
        "[DAP] debugCallback: event={} reason={} file={} line={} col={} bp_id={} debug_state={} configDone={}",
        static_cast<int>(event), reason, filename ? filename : "(null)", line, col, bp_id,
        rt ? JS_DebugGetState(rt) : -1, self->configDone.load());

    if (!self->configDone.load()) {
        /* Buffer the stopped event -- send it after configurationDone */
        core::Logger::instance().debug("[DAP] debugCallback: buffering stopped event (configDone not yet received)");
        self->pendingStop.reason = reason;
        self->pendingStop.threadId = 1;
        self->pendingStop.line = line;
        self->pendingStop.column = col;
        self->pendingStop.bp_id = bp_id;
        self->pendingStop.valid = true;
        return;
    }

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "reason", reason);
    cJSON_AddNumberToObject(body, "threadId", 1);
    cJSON_AddBoolToObject(body, "allThreadsStopped", true);
    if (bp_id > 0) {
        cJSON *ids = cJSON_CreateArray();
        cJSON_AddItemToArray(ids, cJSON_CreateNumber(bp_id));
        cJSON_AddItemToObject(body, "hitBreakpointIds", ids);
    }
    self->pushEvent("stopped", body);

    /* Signal game loop to freeze */
    self->pushFreezeEvent();
}

void DapBridge::pushFreezeEvent() {
    if (_engine && _freezeEventType) {
        SDL_Event e{};
        e.type = static_cast<Uint32>(_freezeEventType);
        SDL_PushEvent(&e);
    }
}

void DapBridge::pushResumeEvent() {
    if (_engine && _resumeEventType) {
        SDL_Event e{};
        e.type = static_cast<Uint32>(_resumeEventType);
        SDL_PushEvent(&e);
    }
}

/* ---- SourceMap support ---- */

SourceMap &DapBridge::getSourceMap(const std::string &jsAbsPath) {
    /* Normalize the path for consistent cache key lookup (lowercase drive letter,
       forward slashes). This ensures that "D:/path" and "d:/path" match. */
    std::string normKey = normalizePath(jsAbsPath.c_str());
    auto it = _sourceMapCache.find(normKey);
    if (it != _sourceMapCache.end()) {
        return it->second;
    }
    /* Parse and cache using the normalized key */
    auto result = _sourceMapCache.emplace(normKey, SourceMap::fromFile(normKey));
    return result.first->second;
}

std::string DapBridge::resolveOriginalSource(const std::string &jsPath, int jsLine,
                                               int &outOrigLine, int &outOrigCol) {
    /* Normalize the JS path to absolute with consistent casing */
    std::string absPath = normalizePath(jsPath.c_str());
    SourceMap &smap = getSourceMap(absPath);

    if (smap.isValid()) {
        outOrigLine = smap.originalLine(jsLine);
        outOrigCol = smap.originalColumn(jsLine, 0);
        return smap.originalPath(absPath);
    }

    /* No source map: return as-is */
    outOrigLine = jsLine;
    outOrigCol = 0;
    return absPath;
}

void DapBridge::warmSourceMapCache() {
    if (!rt) return;
    JSDebugScriptInfo *scripts = nullptr;
    int count = JS_DebugGetLoadedScripts(rt, &scripts);
    for (int i = 0; i < count; i++) {
        if (scripts[i].filename) {
            getSourceMap(normalizePath(scripts[i].filename));
        }
    }
    if (scripts) JS_DebugFreeScriptInfo(rt, scripts, count);
}

std::string DapBridge::resolveBreakpointPath(const std::string &tsPath, int tsLine,
                                              int &outJsLine) {
    if (!rt) {
        outJsLine = tsLine;
        return tsPath;
    }

    std::string normTsPath = normalizePath(tsPath.c_str());
    core::Logger::instance().debug("[DAP] resolveBreakpointPath: normTsPath='{}', tsLine={}", normTsPath, tsLine);

    JSDebugScriptInfo *scripts = nullptr;
    int count = JS_DebugGetLoadedScripts(rt, &scripts);
    core::Logger::instance().debug("[DAP] resolveBreakpointPath: {} loaded scripts", count);

    for (int i = 0; i < count; i++) {
        if (!scripts[i].filename) continue;
        std::string jsPath = normalizePath(scripts[i].filename);
        SourceMap &smap = getSourceMap(jsPath);
        core::Logger::instance().debug("[DAP] resolveBreakpointPath: checking script '{}' smap.valid={}",
                                        scripts[i].filename, smap.isValid());
        if (!smap.isValid()) continue;

        for (int s = 0; s < smap.sourceCount(); s++) {
            core::Logger::instance().debug("[DAP] resolveBreakpointPath:   source[{}]='{}' vs normTsPath='{}'",
                                            s, smap.sourcePath(s), normTsPath);
            if (smap.sourcePath(s) == normTsPath) {
                int genLine = smap.generatedLine(tsLine);
                outJsLine = (genLine > 0) ? genLine : tsLine;
                core::Logger::instance().debug("[DAP] resolveBreakpointPath: MATCH! jsFile='{}' jsLine={}",
                                                scripts[i].filename, outJsLine);
                std::string result = scripts[i].filename;
                if (scripts) JS_DebugFreeScriptInfo(rt, scripts, count);
                return result;
            }
        }
    }

    if (scripts) JS_DebugFreeScriptInfo(rt, scripts, count);

    /* No source map match found: fall back to extension replacement */
    outJsLine = tsLine;
    if (tsPath.size() > 3 && tsPath.substr(tsPath.size() - 3) == ".ts") {
        return tsPath.substr(0, tsPath.size() - 3) + ".js";
    }
    return tsPath;
}

/* ---- Handler thread ---- */

void DapBridge::handlerThreadFunc() {
    while (true) {
        std::string message;
        {
            std::unique_lock<std::mutex> lk(reqMutex);
            reqCv.wait(lk, [this]() {
                return !reqQueue.empty() || shuttingDown.load();
            });
            if (shuttingDown.load() && reqQueue.empty()) break;
            message = std::move(reqQueue.front());
            reqQueue.pop();
        }

        cJSON *msg = cJSON_Parse(message.c_str());
        if (!msg) {
            core::Logger::instance().warn("[DAP] Failed to parse DAP message");
            continue;
        }

        const char *type = json_get_str(msg, "type");
        if (strcmp(type, "request") == 0) {
            dispatchRequest(msg);
        }

        cJSON_Delete(msg);
    }
    core::Logger::instance().debug("[DAP] handler thread exiting");
}

} // namespace noix::debug
