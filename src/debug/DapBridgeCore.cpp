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
    _objectRefs.clear(ctx);
}

/* ---- Transport management ---- */

void DapBridge::closeTransport() {
    /* Close the client socket so the reader thread's blocking read unblocks.
       Also set clientDisconnected flag so tcp_read_byte exits promptly
       even if closesocket() doesn't wake up poll() on Windows. */
    auto *tcp = static_cast<TcpCtx*>(transport.ctx);
    if (tcp) {
        tcp->clientDisconnected.store(true);
        if (tcp->client) {
            NET_DestroyStreamSocket(tcp->client);
            tcp->client = nullptr;
        }
    }
}

void DapBridge::resetSession() {
    /* Reset per-session state so a new client can connect fresh.
       Does NOT close the server socket — that's managed by DapServer. */
    initialized = false;
    launched = false;
    running = false;
    configDone = false;
    stopOnEntry = false;
    firstStop = true;
    pendingStop = {};
    pendingBreakpoints.clear();
    _objectRefs.clear(ctx);
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

/* ---- Core methods ---- */

void DapBridge::setDebugEventTypes(uint32_t freezeType, uint32_t resumeType) {
    _freezeEventType = freezeType;
    _resumeEventType = resumeType;
}

void DapBridge::setTransport(const DapTransport &t) {
    transport = t;
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
    dap_write_message(transport, writeMutex, json);
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
    dap_write_message(transport, writeMutex, json);
}

void DapBridge::enqueueAndWait(std::function<void()> fn) {
    if (!_engine) return;

    std::mutex waitMutex;
    std::condition_variable waitCv;
    bool done = false;

    /* Post the command to the script thread via postTask.
       The script thread's job loop will execute it. */
    _engine->postTask([&]() {
        fn();
        {
            std::lock_guard<std::mutex> wl(waitMutex);
            done = true;
        }
        waitCv.notify_one();
    });

    /* Wait for the script thread to execute the command.
       Use a timeout so we periodically check shuttingDown — this prevents
       deadlock when stopHandlerThread() sets shuttingDown but can only
       notify reqCv (not this local waitCv). */
    std::unique_lock<std::mutex> wl(waitMutex);
    while (!done && !shuttingDown.load()) {
        waitCv.wait_for(wl, std::chrono::milliseconds(100));
    }
}

/* ---- Debug callbacks ---- */

/* drain: called on script thread while paused */
void DapBridge::drainQueue(void *opaque) {
    auto *self = static_cast<DapBridge *>(opaque);
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
    }
}

/* debug callback: called on script thread */
void DapBridge::debugCallback(JSRuntime *rt, JSDebugEventType event,
                                const char *filename, int line, int col,
                                uint32_t bp_id, void *opaque) {
    auto *self = static_cast<DapBridge *>(opaque);

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
