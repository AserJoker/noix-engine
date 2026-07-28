/*
 * DapBridge -- DAP debug bridge for QuickJS, integrated with the noix engine.
 *
 * Extracted from DapTestBridge.cpp. All global state is now member state.
 * Transport code uses the DapBridge::shuttingDown member (via TcpCtx pointer)
 * instead of a global g_shuttingDown.
 *
 * Thread model:
 *   Reader thread  -- reads DAP messages from transport, enqueues requests
 *   Handler thread -- dispatches DAP requests, enqueues script ops
 *   Script thread  -- owned by ScriptEngine, executes JS + debug callbacks
 */

#include "debug/DapBridge.h"
#include "script/ScriptEngine.h"
#include "cJSON.h"
#include "core/Logger.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <fstream>
#include <sstream>
#include <functional>
#include <algorithm>
#include <climits>

/* ---- SDL_net for TCP transport ---- */
#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>

/* ---- Stdio transport helpers ---- */

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

namespace noix::debug {

/* ---- JSON helpers ---- */

static cJSON *json_get(cJSON *obj, const char *key) {
    return obj ? cJSON_GetObjectItemCaseSensitive(obj, key) : nullptr;
}

static int json_get_int(cJSON *obj, const char *key, int def = 0) {
    cJSON *v = json_get(obj, key);
    return v && cJSON_IsNumber(v) ? v->valueint : def;
}

static const char *json_get_str(cJSON *obj, const char *key, const char *def = "") {
    cJSON *v = json_get(obj, key);
    return v && cJSON_IsString(v) ? v->valuestring : def;
}

static bool json_get_bool(cJSON *obj, const char *key, bool def = false) {
    cJSON *v = json_get(obj, key);
    return v && cJSON_IsBool(v) ? cJSON_IsTrue(v) : def;
}

/* ---- Path helpers ---- */

/* Convert a potentially relative path to absolute, using CWD.
   Returns a thread-local static buffer -- use immediately or copy. */
static const char *toAbsolutePath(const char *path) {
    if (!path || !path[0]) return path;
    if (path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':'))
        return path; /* already absolute */
    static thread_local char buf[_MAX_PATH];
    if (_fullpath(buf, path, _MAX_PATH)) return buf;
    return path;
}

/* Normalize a path for comparison: convert to absolute and normalize separators to '/' */
static std::string normalizePath(const char *path) {
    if (!path || !path[0]) return "";
    std::string abs(toAbsolutePath(path));
    std::replace(abs.begin(), abs.end(), '\\', '/');
    /* Lowercase drive letter on Windows for consistency */
    if (abs.size() >= 2 && abs[1] == ':') abs[0] = (char)tolower(abs[0]);
    return abs;
}

/* ---- Minimal module support ---- */

/* Load a file into a malloc'd buffer. Caller must free with js_free(). */
static char *dap_load_file(JSContext *ctx, size_t *pbuf_len, const char *filename) {
    FILE *f;
    fopen_s(&f, filename, "rb");
    if (!f) {
        *pbuf_len = 0;
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)js_malloc(ctx, len + 1);
    if (!buf) {
        fclose(f);
        *pbuf_len = 0;
        return nullptr;
    }
    size_t nread = fread(buf, 1, len, f);
    fclose(f);
    buf[nread] = '\0';
    *pbuf_len = nread;
    return buf;
}

/* Set import.meta.url and import.meta.main on a module.
   Simplified version of js_module_set_import_meta from quickjs-libc. */
static int dap_set_import_meta(JSContext *ctx, JSValueConst func_val,
                                bool is_main) {
    JSModuleDef *m;
    JSValue meta_obj;
    JSAtom module_name_atom;
    const char *module_name;
    char url[1024];

    assert(JS_VALUE_GET_TAG(func_val) == JS_TAG_MODULE);
    m = (JSModuleDef *)JS_VALUE_GET_PTR(func_val);

    module_name_atom = JS_GetModuleName(ctx, m);
    module_name = JS_AtomToCString(ctx, module_name_atom);
    JS_FreeAtom(ctx, module_name_atom);
    if (!module_name)
        return -1;

    /* Build file:// URL */
#ifdef _WIN32
    snprintf(url, sizeof(url), "file:///%s", module_name);
    /* Replace backslashes with forward slashes for a proper URI */
    for (char *p = url + 8; *p; p++) {
        if (*p == '\\') *p = '/';
    }
#else
    snprintf(url, sizeof(url), "file://%s", module_name);
#endif
    JS_FreeCString(ctx, module_name);

    meta_obj = JS_GetImportMeta(ctx, m);
    if (JS_IsException(meta_obj))
        return -1;
    JS_DefinePropertyValueStr(ctx, meta_obj, "url",
                              JS_NewString(ctx, url),
                              JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, meta_obj, "main",
                              JS_NewBool(ctx, is_main),
                              JS_PROP_C_W_E);
    JS_FreeValue(ctx, meta_obj);
    return 0;
}

/* Custom module loader that compiles modules with JS_EVAL_FLAG_DEBUG_INFO
   so that breakpoints work in imported modules.
   Receives DapBridge* as opaque to access scriptPath. */
static JSModuleDef *dap_module_loader(JSContext *ctx, const char *module_name,
                                       void *opaque) {
    auto *bridge = static_cast<DapBridge *>(opaque);

    /* Resolve relative module paths against the main script directory.
       QuickJS default normalizer strips './' but doesn't handle Windows
       backslash paths, so we get a bare filename like 'dap_multifile_mod.js'. */
    std::string resolved_path;
    if (module_name[0] != '/' && module_name[0] != '\\' &&
        !(module_name[0] && module_name[1] == ':')) {
        /* Relative path -- resolve against main script directory */
        std::string base_dir = bridge->scriptPath;
        auto last_sep = base_dir.find_last_of("/\\");
        if (last_sep != std::string::npos)
            base_dir = base_dir.substr(0, last_sep + 1);
        else
            base_dir = "";
        resolved_path = base_dir + module_name;
    } else {
        resolved_path = module_name;
    }

    size_t buf_len;
    char *buf = dap_load_file(ctx, &buf_len, resolved_path.c_str());
    if (!buf) {
        JS_ThrowReferenceError(ctx, "could not load module filename '%s'", resolved_path.c_str());
        return nullptr;
    }

    /* Compile with DEBUG_INFO so breakpoints work in imported modules.
       Use resolved absolute path as the filename so breakpoint matching works. */
    JSValue val = JS_Eval(ctx, buf, buf_len, resolved_path.c_str(),
                          JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY | JS_EVAL_FLAG_DEBUG_INFO);
    js_free(ctx, buf);

    if (JS_IsException(val)) {
        return nullptr;
    }

    JSModuleDef *m = (JSModuleDef *)JS_VALUE_GET_PTR(val);

    /* Set import.meta */
    dap_set_import_meta(ctx, val, false);

    /* Notify about loaded source */
    {
        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "reason", "new");
        cJSON *src = cJSON_CreateObject();
        cJSON_AddStringToObject(src, "name", resolved_path.c_str());
        cJSON_AddStringToObject(src, "path", resolved_path.c_str());
        cJSON_AddNumberToObject(src, "sourceReference", 0);
        cJSON_AddItemToObject(body, "source", src);
        bridge->pushEvent("loadedSource", body);
    }

    /* The module is already referenced by the module system, free our ref */
    JS_FreeValue(ctx, val);
    return m;
}

/* ---- formatJSValue: format a JSValue into DAP variable fields ---- */

static void formatJSValue(DapBridge &bridge, cJSON *v, JSValue val,
                           const char *valueKey = "value") {
    JSContext *ctx = bridge.ctx;

    if (JS_IsNumber(val)) {
        double d;
        JS_ToFloat64(ctx, &d, val);
        char buf[64];
        snprintf(buf, sizeof(buf), "%g", d);
        cJSON_AddStringToObject(v, valueKey, buf);
        cJSON_AddStringToObject(v, "type", "number");
    } else if (JS_IsBool(val)) {
        cJSON_AddStringToObject(v, valueKey, JS_ToBool(ctx, val) ? "true" : "false");
        cJSON_AddStringToObject(v, "type", "boolean");
    } else if (JS_IsString(val)) {
        const char *s = JS_ToCString(ctx, val);
        cJSON_AddStringToObject(v, valueKey, s ? s : "\"\"");
        cJSON_AddStringToObject(v, "type", "string");
        JS_FreeCString(ctx, s);
    } else if (JS_IsNull(val)) {
        cJSON_AddStringToObject(v, valueKey, "null");
        cJSON_AddStringToObject(v, "type", "null");
    } else if (JS_IsUndefined(val)) {
        cJSON_AddStringToObject(v, valueKey, "undefined");
        cJSON_AddStringToObject(v, "type", "undefined");
    } else if (JS_VALUE_GET_TAG(val) == JS_TAG_UNINITIALIZED) {
        /* TDZ -- let/const variable not yet initialized */
        cJSON_AddStringToObject(v, valueKey, "<uninitialized>");
        cJSON_AddStringToObject(v, "type", "undefined");
    } else if (JS_IsObject(val)) {
        /* Check for specific object types */
        if (JS_IsArray(val)) {
            /* Get array length */
            JSAtom lengthAtom = JS_NewAtom(ctx, "length");
            JSValue lenVal = JS_GetProperty(ctx, val, lengthAtom);
            uint32_t len = 0;
            if (JS_IsNumber(lenVal)) {
                int32_t i32;
                if (JS_ToInt32(ctx, &i32, lenVal) == 0)
                    len = (uint32_t)i32;
            }
            JS_FreeValue(ctx, lenVal);
            JS_FreeAtom(ctx, lengthAtom);
            char buf[64];
            snprintf(buf, sizeof(buf), "Array(%u)", len);
            cJSON_AddStringToObject(v, valueKey, buf);
            cJSON_AddStringToObject(v, "type", "object");
        } else if (JS_IsFunction(ctx, val)) {
            cJSON_AddStringToObject(v, valueKey, "function");
            cJSON_AddStringToObject(v, "type", "function");
        } else {
            cJSON_AddStringToObject(v, valueKey, "Object");
            cJSON_AddStringToObject(v, "type", "object");
        }
        int objRef = bridge.addObjectRef(JS_DupValue(ctx, val));
        cJSON_AddNumberToObject(v, "variablesReference", objRef);
    } else {
        cJSON_AddStringToObject(v, valueKey, "[unknown]");
    }
}

/* ---- Stdio transport ---- */

static void init_stdio_binary() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    /* Disable Windows abort() popup -- just terminate silently */
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

static int stdio_read_byte(void *ctx) {
    (void)ctx;
    unsigned char c;
    size_t r = fread(&c, 1, 1, stdin);
    return r == 1 ? (int)c : -1;
}

static void stdio_write_message(void *ctx, const std::string &msg) {
    (void)ctx;
    fprintf(stdout, "%s", msg.c_str());
    fflush(stdout);
}

void init_stdio_transport(DapTransport *t) {
    init_stdio_binary();
    t->readByte = stdio_read_byte;
    t->writeMessage = stdio_write_message;
    t->ctx = nullptr;
}

/* ---- TCP transport ---- */

int tcp_read_byte(void *ctx) {
    auto *t = static_cast<TcpCtx *>(ctx);
    while (t->recvBuffer.empty()) {
        if (!t->client || (t->shuttingDown && t->shuttingDown->load()) || t->clientDisconnected.load()) return -1;
        /* Wait up to 100ms for data; timeout is NOT an error -- just retry.
           Short timeout ensures prompt detection of shutdown/client disconnect. */
        if (!NET_WaitUntilInputAvailable(reinterpret_cast<void **>(&t->client), 1, 100)) {
            if (!t->client || (t->shuttingDown && t->shuttingDown->load()) || t->clientDisconnected.load()) return -1;
            continue; /* Timeout -- no data yet, retry */
        }
        if (!t->client || (t->shuttingDown && t->shuttingDown->load()) || t->clientDisconnected.load()) return -1;
        char buf[4096];
        int n = NET_ReadFromStreamSocket(t->client, buf, sizeof(buf));
        if (n > 0) {
            t->recvBuffer.append(buf, n);
        } else if (n < 0) {
            return -1; /* connection closed or error */
        }
        /* n == 0: no data yet, loop and wait again */
    }
    int c = static_cast<unsigned char>(t->recvBuffer[0]);
    t->recvBuffer.erase(0, 1);
    return c;
}

void tcp_write_message(void *ctx, const std::string &msg) {
    auto *t = static_cast<TcpCtx *>(ctx);
    if (!t->client) return;
    NET_WriteToStreamSocket(t->client, msg.data(), static_cast<int>(msg.size()));
    NET_WaitUntilStreamSocketDrained(t->client, 500);
}

bool init_tcp_transport(DapTransport *t, TcpCtx *tcp, int port,
                         std::atomic<bool> &shuttingDown) {
    tcp->port = port;
    tcp->server = nullptr;
    tcp->client = nullptr;
    tcp->recvBuffer.clear();
    tcp->shuttingDown = &shuttingDown;

    if (!NET_Init()) {
        core::Logger::instance().error("NET_Init failed: {}", SDL_GetError());
        return false;
    }

    tcp->server = NET_CreateServer(nullptr, port, 0);
    if (!tcp->server) {
        core::Logger::instance().error("NET_CreateServer failed: {}", SDL_GetError());
        NET_Quit();
        return false;
    }

    core::Logger::instance().info("DAP bridge listening on port {}, waiting for connection...", port);

    if (!tcp_accept_client(tcp)) return false;

    t->readByte = tcp_read_byte;
    t->writeMessage = tcp_write_message;
    t->ctx = tcp;
    return true;
}

bool tcp_accept_client(TcpCtx *tcp) {
    tcp->clientDisconnected.store(false);
    while (!tcp->client && !(tcp->shuttingDown && tcp->shuttingDown->load())) {
        NET_AcceptClient(tcp->server, &tcp->client);
        if (!tcp->client) {
            SDL_Delay(50);
        }
    }
    return tcp->client != nullptr;
}

void cleanup_tcp(TcpCtx *tcp) {
    if (tcp->client) {
        NET_DestroyStreamSocket(static_cast<NET_StreamSocket *>(tcp->client));
        tcp->client = nullptr;
    }
    if (tcp->server) {
        NET_DestroyServer(static_cast<NET_Server *>(tcp->server));
        tcp->server = nullptr;
    }
    NET_Quit();
}

/* ---- DAP wire protocol ---- */

bool dap_read_message(DapTransport &transport, std::string &out) {
    /* Read headers until empty line (\r\n\r\n) */
    int content_length = -1;
    std::string header_buf;

    while (true) {
        int c = transport.readByte(transport.ctx);
        if (c == -1) return false;
        if (c == '\r') {
            int c2 = transport.readByte(transport.ctx);
            if (c2 == '\n') {
                /* End of header line */
                if (header_buf.empty()) {
                    /* Empty line = end of headers */
                    break;
                }
                /* Parse header */
                if (header_buf.compare(0, 15, "Content-Length:") == 0) {
                    content_length = atoi(header_buf.c_str() + 15);
                }
                header_buf.clear();
            } else {
                header_buf += (char)c;
                if (c2 != -1) header_buf += (char)c2;
            }
        } else {
            header_buf += (char)c;
        }
    }

    if (content_length <= 0) return false;

    out.resize(content_length);
    for (int i = 0; i < content_length; i++) {
        int c = transport.readByte(transport.ctx);
        if (c == -1) return false;
        out[i] = (char)c;
    }
    return true;
}

void dap_write_message(DapTransport &transport, std::mutex &writeMutex,
                        const std::string &json) {
    std::lock_guard<std::mutex> lk(writeMutex);
    std::string msg = "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n" + json;
    transport.writeMessage(transport.ctx, msg);
}

/* ---- DapBridge constructor / destructor ---- */

DapBridge::DapBridge() = default;

DapBridge::~DapBridge() {
    stopHandlerThread();
    clearObjectRefs();
}

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
    clearObjectRefs();
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

/* ---- DapBridge methods ---- */

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

int DapBridge::addObjectRef(JSValue obj) {
    int ref = nextObjectVarRef++;
    objectRefs.push_back({ref, obj});
    return ref;
}

JSValue DapBridge::findObjectRef(int varRef) {
    for (auto &or_ref : objectRefs) {
        if (or_ref.varRef == varRef)
            return or_ref.obj;
    }
    return JS_UNDEFINED;
}

void DapBridge::clearObjectRefs() {
    for (auto &or_ref : objectRefs) {
        if (ctx) JS_FreeValue(ctx, or_ref.obj);
    }
    objectRefs.clear();
    nextObjectVarRef = 100000;
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

/* ---- DAP request handlers ---- */

void DapBridge::handleInitialize(cJSON *args, int requestSeq) {
    (void)args;
    initialized = true;

    cJSON *body = cJSON_CreateObject();
    cJSON_AddBoolToObject(body, "supportsConfigurationDoneRequest", true);
    cJSON_AddBoolToObject(body, "supportsConditionalBreakpoints", true);
    cJSON_AddBoolToObject(body, "supportsExceptionInfoRequest", true);
    cJSON_AddBoolToObject(body, "supportsSetVariable", false);
    cJSON_AddBoolToObject(body, "supportsLoadedSourcesRequest", true);

    cJSON *filters = cJSON_CreateArray();
    {
        cJSON *f1 = cJSON_CreateObject();
        cJSON_AddStringToObject(f1, "filter", "all");
        cJSON_AddStringToObject(f1, "label", "All Exceptions");
        cJSON_AddItemToArray(filters, f1);
    }
    {
        cJSON *f2 = cJSON_CreateObject();
        cJSON_AddStringToObject(f2, "filter", "uncaught");
        cJSON_AddStringToObject(f2, "label", "Uncaught Exceptions");
        cJSON_AddItemToArray(filters, f2);
    }
    cJSON_AddItemToObject(body, "exceptionBreakpointFilters", filters);

    sendResponse(requestSeq, "initialize", true, nullptr, body);

    /* DAP protocol requires an 'initialized' event after the initialize response */
    pushEvent("initialized", cJSON_CreateObject());
}

void DapBridge::handleLaunch(cJSON *args, int requestSeq, const char *commandName) {
    const char *script = json_get_str(args, "script");
    if (script && script[0]) {
        scriptPath = normalizePath(script);
    } else if (_engine) {
        /* attach mode: default to scriptsPath/entry.js */
        scriptPath = normalizePath((_engine->scriptsPath() + "/entry.js").c_str());
    }
    stopOnEntry = json_get_bool(args, "stopOnEntry", false);

    if (scriptPath.empty()) {
        sendResponse(requestSeq, commandName, false, "no script path", nullptr);
        return;
    }

    launched = true;
    running = true;
    firstStop = true;

    /* In the new architecture, the script is already running (loaded by
       ScriptEngine at startup). The debugger just attaches to observe.
       Do NOT call executeScript() here — it would re-evaluate the script. */

    sendResponse(requestSeq, commandName, true, nullptr, nullptr);
}

void DapBridge::handleDisconnect(int requestSeq) {
    core::Logger::instance().debug("[DAP] handleDisconnect: enter, running={}, rt={}",
                                    running.load(), (void*)rt);
    running = false;

    if (rt) {
        JS_DebugContinue(rt);
    }

    /* Send response and terminated event before closing the socket.
       All writes are protected by writeMutex to avoid racing with closeTransport. */
    sendResponse(requestSeq, "disconnect", true, nullptr, nullptr);
    pushEvent("terminated", cJSON_CreateObject());

    /* Close the client socket under writeMutex to ensure no concurrent writes.
       Also sets clientDisconnected so tcp_read_byte exits promptly. */
    {
        std::lock_guard<std::mutex> lk(writeMutex);
        closeTransport();
    }

    /* Push resume event so game loop un-freezes.
       Socket is now closed; pushResumeEvent's write will be a no-op (client is null),
       but the game loop unfreezing is handled by SDL events, not DAP writes. */
    pushResumeEvent();
}

void DapBridge::handleSetBreakpoints(cJSON *args, int requestSeq) {
    cJSON *source = json_get(args, "source");
    const char *path = json_get_str(source, "path");
    cJSON *bps = json_get(args, "breakpoints");

    /* Collect requested breakpoint lines/conditions before any enqueueAndWait */
    struct BpReq { int line; std::string condition; };
    std::vector<BpReq> requestedBps;
    if (bps && cJSON_IsArray(bps)) {
        int arrSize = cJSON_GetArraySize(bps);
        for (int bi = 0; bi < arrSize; bi++) {
            cJSON *bp = cJSON_GetArrayItem(bps, bi);
            BpReq req;
            req.line = json_get_int(bp, "line");
            req.condition = json_get_str(bp, "condition", "");
            requestedBps.push_back(req);
        }
    }

    /* Results array -- filled during script-thread execution or for pending */
    cJSON *result = cJSON_CreateArray();

    /* Normalize the client-provided path for comparison */
    std::string normPath = normalizePath(path);

    /* Helper: check if a breakpoint filename matches the client path */
    auto pathMatches = [&](const std::string &bpFilename) -> bool {
        return normalizePath(bpFilename.c_str()) == normPath;
    };

    if (rt && running) {
        /* Runtime exists and script is running -- do everything on the script thread
           to avoid race conditions and ensure breakpoint line correction works */
        enqueueAndWait([&]() {
            /* Resolve client path to QuickJS internal filename */
            std::string resolvedPath(path);
            JSDebugScriptInfo *scripts = nullptr;
            int scriptCount = JS_DebugGetLoadedScripts(rt, &scripts);
            for (int si = 0; si < scriptCount; si++) {
                if (normalizePath(scripts[si].filename) == normPath) {
                    resolvedPath = scripts[si].filename;
                    break;
                }
            }
            if (scripts) JS_DebugFreeScriptInfo(rt, scripts, scriptCount);

            /* Remove existing breakpoints for this file */
            for (auto it = breakpoints.begin(); it != breakpoints.end(); ) {
                if (pathMatches(it->filename)) {
                    JS_DebugRemoveBreakpoint(rt, it->id);
                    it = breakpoints.erase(it);
                } else {
                    ++it;
                }
            }

            /* Set new breakpoints */
            for (auto &req : requestedBps) {
                uint32_t id;
                if (!req.condition.empty()) {
                    id = JS_DebugSetConditionalBreakpoint(rt, resolvedPath.c_str(), req.line, req.condition.c_str());
                } else {
                    id = JS_DebugSetBreakpoint(rt, resolvedPath.c_str(), req.line);
                }

                if (id > 0) {
                    Breakpoint b;
                    b.id = id;
                    b.filename = resolvedPath;
                    b.line = req.line;
                    b.condition = req.condition;
                    b.verified = true;
                    breakpoints.push_back(b);

                    cJSON *r = cJSON_CreateObject();
                    cJSON_AddBoolToObject(r, "verified", true);
                    cJSON_AddNumberToObject(r, "id", id);
                    cJSON_AddNumberToObject(r, "line", req.line);
                    cJSON_AddItemToArray(result, r);
                } else {
                    cJSON *r = cJSON_CreateObject();
                    cJSON_AddBoolToObject(r, "verified", false);
                    cJSON_AddNumberToObject(r, "line", req.line);
                    cJSON_AddItemToArray(result, r);
                }
            }
        });
    } else if (rt) {
        /* Runtime exists but not running -- set directly on current thread */
        /* Resolve client path to QuickJS internal filename */
        JSDebugScriptInfo *scripts = nullptr;
        int scriptCount = JS_DebugGetLoadedScripts(rt, &scripts);
        std::string resolvedPath(path);
        for (int si = 0; si < scriptCount; si++) {
            if (normalizePath(scripts[si].filename) == normPath) {
                resolvedPath = scripts[si].filename;
                break;
            }
        }
        if (scripts) JS_DebugFreeScriptInfo(rt, scripts, scriptCount);

        for (auto it = breakpoints.begin(); it != breakpoints.end(); ) {
            if (pathMatches(it->filename)) {
                JS_DebugRemoveBreakpoint(rt, it->id);
                it = breakpoints.erase(it);
            } else {
                ++it;
            }
        }
        for (auto &req : requestedBps) {
            uint32_t id;
            if (!req.condition.empty()) {
                id = JS_DebugSetConditionalBreakpoint(rt, resolvedPath.c_str(), req.line, req.condition.c_str());
            } else {
                id = JS_DebugSetBreakpoint(rt, resolvedPath.c_str(), req.line);
            }
            if (id > 0) {
                Breakpoint b;
                b.id = id;
                b.filename = resolvedPath;
                b.line = req.line;
                b.condition = req.condition;
                b.verified = true;
                breakpoints.push_back(b);

                cJSON *r = cJSON_CreateObject();
                cJSON_AddBoolToObject(r, "verified", true);
                cJSON_AddNumberToObject(r, "id", id);
                cJSON_AddNumberToObject(r, "line", req.line);
                cJSON_AddItemToArray(result, r);
            } else {
                cJSON *r = cJSON_CreateObject();
                cJSON_AddBoolToObject(r, "verified", false);
                cJSON_AddNumberToObject(r, "line", req.line);
                cJSON_AddItemToArray(result, r);
            }
        }
    } else {
        /* No runtime yet -- save as pending */
        for (auto it = breakpoints.begin(); it != breakpoints.end(); ) {
            if (pathMatches(it->filename))
                it = breakpoints.erase(it);
            else
                ++it;
        }
        for (auto it = pendingBreakpoints.begin(); it != pendingBreakpoints.end(); ) {
            if (pathMatches(it->filename))
                it = pendingBreakpoints.erase(it);
            else
                ++it;
        }
        for (auto &req : requestedBps) {
            PendingBreakpoint pb;
            pb.filename = path;
            pb.line = req.line;
            pb.condition = req.condition;
            pendingBreakpoints.push_back(pb);

            cJSON *r = cJSON_CreateObject();
            cJSON_AddBoolToObject(r, "verified", true);
            cJSON_AddNumberToObject(r, "line", req.line);
            cJSON_AddItemToArray(result, r);
        }
    }

    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "breakpoints", result);
    sendResponse(requestSeq, "setBreakpoints", true, nullptr, body);
}

void DapBridge::handleSetExceptionBreakpoints(cJSON *args, int requestSeq) {
    int state = 0; /* 0=off */

    if (args) {
        cJSON *filters = cJSON_GetObjectItemCaseSensitive(args, "filters");
        if (filters && cJSON_IsArray(filters)) {
            int arrSize = cJSON_GetArraySize(filters);
            for (int i = 0; i < arrSize; i++) {
                cJSON *f = cJSON_GetArrayItem(filters, i);
                if (f && cJSON_IsString(f) && f->valuestring) {
                    if (strcmp(f->valuestring, "all") == 0)
                        state = 2;
                    else if (strcmp(f->valuestring, "uncaught") == 0)
                        state = (state < 1) ? 1 : state;
                }
            }
        }
    }

    pendingExceptionState = state;

    if (rt) {
        JS_DebugSetPauseOnExceptions(rt, state);
    }

    sendResponse(requestSeq, "setExceptionBreakpoints", true, nullptr, nullptr);
}

void DapBridge::handleContinue(int requestSeq) {
    core::Logger::instance().debug("[DAP] handleContinue: rt={}", (void*)rt);
    enqueueAndWait([this]() {
        core::Logger::instance().debug("[DAP] enqueueAndWait: calling JS_DebugContinue, rt={}", (void*)rt);
        JS_DebugContinue(rt);
    });
    core::Logger::instance().debug("[DAP] handleContinue: done");

    /* Push resume event so game loop un-freezes */
    pushResumeEvent();

    cJSON *body = cJSON_CreateObject();
    cJSON_AddBoolToObject(body, "allThreadsContinued", true);
    sendResponse(requestSeq, "continue", true, nullptr, body);
}

void DapBridge::handleNext(int requestSeq) {
    core::Logger::instance().debug("[DAP] handleNext: enter, rt={}, debug_state={}",
                                    (void*)rt, rt ? JS_DebugGetState(rt) : -1);
    enqueueAndWait([this]() {
        core::Logger::instance().debug("[DAP] handleNext: enqueueAndWait executing, calling JS_DebugStep");
        JS_DebugStep(rt, 1); /* step over */
        core::Logger::instance().debug("[DAP] handleNext: JS_DebugStep done, debug_state={}",
                                        JS_DebugGetState(rt));
    });
    core::Logger::instance().debug("[DAP] handleNext: enqueueAndWait returned");

    pushResumeEvent();
    sendResponse(requestSeq, "next", true, nullptr, nullptr);
    core::Logger::instance().debug("[DAP] handleNext: response sent");
}

void DapBridge::handleStepIn(int requestSeq) {
    core::Logger::instance().debug("[DAP] handleStepIn: enter, rt={}, debug_state={}",
                                    (void*)rt, rt ? JS_DebugGetState(rt) : -1);
    enqueueAndWait([this]() {
        JS_DebugStep(rt, 0); /* step into */
    });

    pushResumeEvent();
    sendResponse(requestSeq, "stepIn", true, nullptr, nullptr);
}

void DapBridge::handleStepOut(int requestSeq) {
    core::Logger::instance().debug("[DAP] handleStepOut: enter, rt={}, debug_state={}",
                                    (void*)rt, rt ? JS_DebugGetState(rt) : -1);
    enqueueAndWait([this]() {
        JS_DebugStep(rt, 2); /* step out */
    });

    pushResumeEvent();
    sendResponse(requestSeq, "stepOut", true, nullptr, nullptr);
}

void DapBridge::handlePause(int requestSeq) {
    if (rt) {
        JS_DebugPause(rt);
    }
    sendResponse(requestSeq, "pause", true, nullptr, nullptr);
}

void DapBridge::handleStackTrace(cJSON *args, int requestSeq) {
    int startFrame = json_get_int(args, "startFrame", 0);
    int levels = json_get_int(args, "levels", 0);

    JSDebugFrameInfo *frames = nullptr;
    int count = 0;

    enqueueAndWait([&]() {
        count = JS_DebugCaptureStack(rt, &frames);
    });

    if (levels <= 0) levels = count - startFrame;
    int endFrame = startFrame + levels;
    if (endFrame > count) endFrame = count;

    cJSON *stackFrames = cJSON_CreateArray();
    for (int i = startFrame; i < endFrame; i++) {
        cJSON *f = cJSON_CreateObject();
        cJSON_AddNumberToObject(f, "id", i);
        cJSON_AddStringToObject(f, "name", frames[i].func_name ? frames[i].func_name : "<anonymous>");
        cJSON *src = cJSON_CreateObject();
        const char *fname = frames[i].filename ? frames[i].filename : "<unknown>";
        cJSON_AddStringToObject(src, "name", fname);
        cJSON_AddStringToObject(src, "path", toAbsolutePath(fname));
        cJSON_AddNumberToObject(src, "sourceReference", 0);
        cJSON_AddItemToObject(f, "source", src);
        cJSON_AddNumberToObject(f, "line", frames[i].line);
        cJSON_AddNumberToObject(f, "column", frames[i].col);
        cJSON_AddItemToArray(stackFrames, f);
    }

    if (frames) JS_DebugFreeFrameInfo(rt, frames, count);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "stackFrames", stackFrames);
    cJSON_AddNumberToObject(body, "totalFrames", count);
    sendResponse(requestSeq, "stackTrace", true, nullptr, body);
}

void DapBridge::handleScopes(cJSON *args, int requestSeq) {
    int frameId = json_get_int(args, "frameId", 0);

    JSDebugScopeInfo *scopes = nullptr;
    int scopeCount = 0;

    enqueueAndWait([&]() {
        scopeCount = JS_DebugGetFrameScopes(rt, frameId, &scopes);
    });

    cJSON *scopesArr = cJSON_CreateArray();

    /* We categorize scopes into: Closure (any that are before the frame's
       own scopes), Local (the frame's own scopes), and Global.
       We merge adjacent scopes of the same category into one DAP scope. */
    bool hasLocal = false, hasClosure = false, hasGlobal = false;
    int localVarCount = 0, closureVarCount = 0;

    for (int i = 0; i < scopeCount; i++) {
        if (scopes[i].scope_level == -1) {
            hasGlobal = true;
        } else {
            const char *cat = scopes[i].name ? scopes[i].name : "Local";
            if (strcmp(cat, "Closure") == 0) {
                hasClosure = true;
                closureVarCount += scopes[i].var_count;
            } else {
                hasLocal = true;
                localVarCount += scopes[i].var_count;
            }
        }
    }

    /* Add Closure scope first (if any) */
    if (hasClosure) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "name", "Closure");
        cJSON_AddNumberToObject(s, "variablesReference", frameId * 100 + SCOPE_REF_CLOSURE);
        cJSON_AddNumberToObject(s, "namedVariables", closureVarCount);
        cJSON_AddStringToObject(s, "presentationHint", "closure");
        cJSON_AddItemToArray(scopesArr, s);
    }

    /* Add Local scope */
    if (hasLocal) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "name", "Local");
        cJSON_AddNumberToObject(s, "variablesReference", frameId * 100 + SCOPE_REF_LOCAL);
        cJSON_AddNumberToObject(s, "namedVariables", localVarCount);
        cJSON_AddStringToObject(s, "presentationHint", "locals");
        cJSON_AddItemToArray(scopesArr, s);
    }

    /* Add Global scope */
    if (hasGlobal) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "name", "Global");
        cJSON_AddNumberToObject(s, "variablesReference", frameId * 100 + SCOPE_REF_GLOBAL);
        cJSON_AddNumberToObject(s, "namedVariables", 0);
        cJSON_AddStringToObject(s, "presentationHint", "globals");
        cJSON_AddItemToArray(scopesArr, s);
    }

    if (scopes) JS_DebugFreeScopeInfo(rt, scopes, scopeCount);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "scopes", scopesArr);
    sendResponse(requestSeq, "scopes", true, nullptr, body);
}

void DapBridge::handleVariables(cJSON *args, int requestSeq) {
    int varRef = json_get_int(args, "variablesReference", 0);

    /* Object expansion: varRef >= 100000 */
    if (varRef >= 100000) {
        JSValue obj = findObjectRef(varRef);
        cJSON *varsArr = cJSON_CreateArray();

        if (JS_IsObject(obj)) {
            enqueueAndWait([&]() {
                JSPropertyEnum *props = nullptr;
                uint32_t propCount = 0;
                JS_GetOwnPropertyNames(ctx, &props, &propCount, obj,
                                       JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY);
                for (uint32_t i = 0; i < propCount; i++) {
                    const char *name = JS_AtomToCString(ctx, props[i].atom);
                    JSValue propVal = JS_GetProperty(ctx, obj, props[i].atom);

                    cJSON *v = cJSON_CreateObject();
                    cJSON_AddStringToObject(v, "name", name ? name : "");
                    formatJSValue(*this, v, propVal);
                    if (!JS_IsObject(propVal)) {
                        cJSON_AddNumberToObject(v, "variablesReference", 0);
                    }
                    cJSON_AddItemToArray(varsArr, v);

                    JS_FreeCString(ctx, name);
                    JS_FreeValue(ctx, propVal);
                    JS_FreeAtom(ctx, props[i].atom);
                }
                js_free(ctx, props);
            });
        }

        cJSON *body = cJSON_CreateObject();
        cJSON_AddItemToObject(body, "variables", varsArr);
        sendResponse(requestSeq, "variables", true, nullptr, body);
        return;
    }

    /* Decode frameId and scope type from varRef.
       Encoding: frameId * 100 + SCOPE_REF_{LOCAL,CLOSURE,GLOBAL} */
    int frameId = varRef / 100;
    int scopeType = varRef % 100;

    if (scopeType == SCOPE_REF_GLOBAL) {
        /* Enumerate global object properties */
        cJSON *varsArr = cJSON_CreateArray();
        enqueueAndWait([&]() {
            JSValue globalObj = JS_GetGlobalObject(ctx);
            JSPropertyEnum *props = nullptr;
            uint32_t propCount = 0;
            JS_GetOwnPropertyNames(ctx, &props, &propCount, globalObj,
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY);
            for (uint32_t i = 0; i < propCount; i++) {
                const char *name = JS_AtomToCString(ctx, props[i].atom);
                JSValue propVal = JS_GetProperty(ctx, globalObj, props[i].atom);

                cJSON *v = cJSON_CreateObject();
                cJSON_AddStringToObject(v, "name", name ? name : "");
                formatJSValue(*this, v, propVal);
                if (!JS_IsObject(propVal)) {
                    cJSON_AddNumberToObject(v, "variablesReference", 0);
                }
                cJSON_AddItemToArray(varsArr, v);

                JS_FreeCString(ctx, name);
                JS_FreeValue(ctx, propVal);
                JS_FreeAtom(ctx, props[i].atom);
            }
            js_free(ctx, props);
            JS_FreeValue(ctx, globalObj);
        });
        cJSON *body = cJSON_CreateObject();
        cJSON_AddItemToObject(body, "variables", varsArr);
        sendResponse(requestSeq, "variables", true, nullptr, body);
        return;
    }

    if (scopeType == SCOPE_REF_CLOSURE) {
        /* Closure scope: collect variables from enclosing frames.
           Walk the call stack from outermost to the frame just above the
           current one, collecting their locals. Inner frame vars override
           outer ones (same name). */
        cJSON *varsArr = cJSON_CreateArray();
        enqueueAndWait([&]() {
            /* Determine total frame count */
            int totalFrames = 0;
            for (int f = 0; ; f++) {
                JSDebugVarInfo *test = nullptr;
                int fc = JS_DebugGetFrameLocals(rt, f, &test);
                if (fc < 0) break;
                if (test) JS_DebugFreeVarInfo(ctx, test, fc);
                totalFrames++;
            }

            /* Collect from outer frames (frames deeper than frameId) */
            std::vector<std::pair<std::string, JSValue>> closureVars;
            for (int f = totalFrames - 1; f > frameId; f--) {
                JSDebugVarInfo *fvars = nullptr;
                int fvarCount = JS_DebugGetFrameLocals(rt, f, &fvars);
                for (int j = 0; j < fvarCount; j++) {
                    if (fvars[j].name)
                        closureVars.push_back({fvars[j].name, JS_DupValue(ctx, fvars[j].value)});
                }
                if (fvars) JS_DebugFreeVarInfo(ctx, fvars, fvarCount);
            }

            /* Deduplicate: keep last occurrence (innermost frame wins) */
            std::vector<bool> skip(closureVars.size(), false);
            for (int a = 0; a < (int)closureVars.size(); a++) {
                for (int b = a + 1; b < (int)closureVars.size(); b++) {
                    if (!skip[b] && closureVars[a].first == closureVars[b].first)
                        skip[a] = true;
                }
            }

            for (int a = 0; a < (int)closureVars.size(); a++) {
                if (skip[a]) {
                    JS_FreeValue(ctx, closureVars[a].second);
                    continue;
                }
                cJSON *v = cJSON_CreateObject();
                cJSON_AddStringToObject(v, "name", closureVars[a].first.c_str());
                formatJSValue(*this, v, closureVars[a].second);
                if (!JS_IsObject(closureVars[a].second)) {
                    cJSON_AddNumberToObject(v, "variablesReference", 0);
                }
                cJSON_AddItemToArray(varsArr, v);
                JS_FreeValue(ctx, closureVars[a].second);
            }
        });
        cJSON *body = cJSON_CreateObject();
        cJSON_AddItemToObject(body, "variables", varsArr);
        sendResponse(requestSeq, "variables", true, nullptr, body);
        return;
    }

    /* SCOPE_REF_LOCAL: return ALL frame locals for this frame.
       QuickJS's per-scope var_start/var_count tracking is unreliable,
       so for Local scopes we just return the full flat variable list
       from JS_DebugGetFrameLocals. This ensures all local variables
       are visible even when the scope partitioning is incorrect.

       IMPORTANT: All QuickJS API calls (including formatJSValue which
       calls JS_IsObject, JS_DupValue etc.) must run inside enqueueAndWait
       on the script thread. */
    cJSON *varsArr = cJSON_CreateArray();
    enqueueAndWait([&]() {
        JSDebugVarInfo *vars = nullptr;
        int varCount = JS_DebugGetFrameLocals(rt, frameId, &vars);

        for (int i = 0; i < varCount; i++) {
            cJSON *v = cJSON_CreateObject();
            cJSON_AddStringToObject(v, "name", vars[i].name ? vars[i].name : "<var>");

            formatJSValue(*this, v, vars[i].value);
            if (!JS_IsObject(vars[i].value)) {
                cJSON_AddNumberToObject(v, "variablesReference", 0);
            }

            cJSON_AddItemToArray(varsArr, v);
        }

        if (vars) JS_DebugFreeVarInfo(ctx, vars, varCount);
    });

    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "variables", varsArr);
    sendResponse(requestSeq, "variables", true, nullptr, body);
}

void DapBridge::handleEvaluate(cJSON *args, int requestSeq) {
    const char *expr = json_get_str(args, "expression");
    const char *context = json_get_str(args, "context", "repl");
    (void)context;
    int frameId = json_get_int(args, "frameId", 0);

    JSValue result = JS_UNDEFINED;
    bool success = false;

    enqueueAndWait([&]() {
        result = JS_DebugEvaluateOnFrameScoped(rt, frameId, expr);
        success = !JS_IsException(result);
    });

    cJSON *body = cJSON_CreateObject();
    if (success) {
        formatJSValue(*this, body, result, "result");
        if (!JS_IsObject(result)) {
            cJSON_AddNumberToObject(body, "variablesReference", 0);
        }
        /* objects already have variablesReference set by formatJSValue */
    } else {
        JSValue exc = JS_GetException(ctx);
        const char *str = JS_ToCString(ctx, exc);
        cJSON_AddStringToObject(body, "result", str ? str : "Error");
        JS_FreeCString(ctx, str);
        JS_FreeValue(ctx, exc);
    }
    JS_FreeValue(ctx, result);

    sendResponse(requestSeq, "evaluate", success, nullptr, body);
}

void DapBridge::handleThreads(int requestSeq) {
    cJSON *body = cJSON_CreateObject();
    cJSON *threads = cJSON_CreateArray();
    cJSON *t = cJSON_CreateObject();
    cJSON_AddNumberToObject(t, "id", 1);
    cJSON_AddStringToObject(t, "name", "Main Thread");
    cJSON_AddItemToArray(threads, t);
    cJSON_AddItemToObject(body, "threads", threads);
    sendResponse(requestSeq, "threads", true, nullptr, body);
}

void DapBridge::handleSource(cJSON *args, int requestSeq) {
    cJSON *srcArg = json_get(args, "source");
    const char *path = json_get_str(srcArg, "path");

    if (!path || !path[0]) {
        sendResponse(requestSeq, "source", false, "no source path", nullptr);
        return;
    }

    /* Read the file from disk */
    const char *absPath = toAbsolutePath(path);
    std::ifstream file(absPath);
    if (!file.is_open()) {
        sendResponse(requestSeq, "source", false, "cannot open file", nullptr);
        return;
    }
    std::stringstream ss;
    ss << file.rdbuf();

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "content", ss.str().c_str());
    cJSON_AddStringToObject(body, "mimeType", "text/javascript");
    sendResponse(requestSeq, "source", true, nullptr, body);
}

void DapBridge::handleLoadedSources(int requestSeq) {
    JSDebugScriptInfo *scripts = nullptr;
    int count = 0;

    if (rt) {
        enqueueAndWait([&]() {
            count = JS_DebugGetLoadedScripts(rt, &scripts);
        });
    }

    cJSON *sources = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "name", scripts[i].filename);
        cJSON_AddStringToObject(s, "path", toAbsolutePath(scripts[i].filename));
        cJSON_AddNumberToObject(s, "sourceReference", 0);
        cJSON_AddItemToArray(sources, s);
    }

    if (scripts) JS_DebugFreeScriptInfo(rt, scripts, count);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "sources", sources);
    sendResponse(requestSeq, "loadedSources", true, nullptr, body);
}

/* ---- Request dispatch ---- */

void DapBridge::dispatchRequest(cJSON *msg) {
    int requestSeq = json_get_int(msg, "seq");
    const char *command = json_get_str(msg, "command");
    cJSON *args = json_get(msg, "arguments");

    core::Logger::instance().debug("[DAP] >>> request: seq={} command={}", requestSeq, command);

    if (strcmp(command, "initialize") == 0) {
        handleInitialize(args, requestSeq);
    } else if (strcmp(command, "launch") == 0) {
        handleLaunch(args, requestSeq);
    } else if (strcmp(command, "attach") == 0) {
        /* attach reuses launch logic; script path comes from --script CLI arg */
        handleLaunch(args, requestSeq, "attach");
    } else if (strcmp(command, "disconnect") == 0) {
        handleDisconnect(requestSeq);
    } else if (strcmp(command, "setBreakpoints") == 0) {
        handleSetBreakpoints(args, requestSeq);
    } else if (strcmp(command, "setExceptionBreakpoints") == 0) {
        handleSetExceptionBreakpoints(args, requestSeq);
    } else if (strcmp(command, "configurationDone") == 0) {
        configDone = true;
        sendResponse(requestSeq, "configurationDone", true, nullptr, nullptr);
        /* If a stopped event was buffered before configurationDone, send it now */
        if (pendingStop.valid) {
            core::Logger::instance().debug("[DAP] configurationDone: sending buffered stopped event");
            cJSON *body = cJSON_CreateObject();
            cJSON_AddStringToObject(body, "reason", pendingStop.reason.c_str());
            cJSON_AddNumberToObject(body, "threadId", pendingStop.threadId);
            cJSON_AddBoolToObject(body, "allThreadsStopped", true);
            if (pendingStop.bp_id > 0) {
                cJSON *ids = cJSON_CreateArray();
                cJSON_AddItemToArray(ids, cJSON_CreateNumber(pendingStop.bp_id));
                cJSON_AddItemToObject(body, "hitBreakpointIds", ids);
            }
            pushEvent("stopped", body);
            pendingStop.valid = false;
        }
    } else if (strcmp(command, "continue") == 0) {
        handleContinue(requestSeq);
    } else if (strcmp(command, "next") == 0) {
        handleNext(requestSeq);
    } else if (strcmp(command, "stepIn") == 0) {
        handleStepIn(requestSeq);
    } else if (strcmp(command, "stepOut") == 0) {
        handleStepOut(requestSeq);
    } else if (strcmp(command, "pause") == 0) {
        handlePause(requestSeq);
    } else if (strcmp(command, "stackTrace") == 0) {
        handleStackTrace(args, requestSeq);
    } else if (strcmp(command, "scopes") == 0) {
        handleScopes(args, requestSeq);
    } else if (strcmp(command, "variables") == 0) {
        handleVariables(args, requestSeq);
    } else if (strcmp(command, "evaluate") == 0) {
        handleEvaluate(args, requestSeq);
    } else if (strcmp(command, "threads") == 0) {
        handleThreads(requestSeq);
    } else if (strcmp(command, "loadedSources") == 0) {
        handleLoadedSources(requestSeq);
    } else if (strcmp(command, "source") == 0) {
        handleSource(args, requestSeq);
    } else {
        core::Logger::instance().warn("[DAP] unknown command: {}", command);
        sendResponse(requestSeq, command, false, "unknown command", nullptr);
    }
}

/* ---- executeScript: called by ScriptEngine on the script thread ---- */

void DapBridge::executeScript() {
    /* Read script file */
    std::ifstream file(scriptPath);
    if (!file.is_open()) {
        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "category", "stderr");
        cJSON_AddStringToObject(body, "output", "Cannot open script file");
        pushEvent("output", body);
        running = false;
        return;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    /* Set debug callbacks */
    JS_SetDebugCallback(rt, DapBridge::debugCallback, this);
    JS_SetDebugDrainQueue(rt, DapBridge::drainQueue);

    /* Set module loader -- custom loader adds JS_EVAL_FLAG_DEBUG_INFO
       so breakpoints work in imported modules. Pass 'this' as opaque
       so the loader can access scriptPath for relative resolution. */
    JS_SetModuleLoaderFunc(rt, nullptr, dap_module_loader, this);

    /* Apply pending exception breakpoint state */
    if (pendingExceptionState > 0) {
        JS_DebugSetPauseOnExceptions(rt, pendingExceptionState);
    }

    /* Install console API -- redirect console.log to DAP output events.
       Use a thread-local pointer to the DapBridge instance since the
       JS_NewCFunction callback is a plain C function and cannot capture
       'this' directly. */
    {
        static thread_local DapBridge *tl_bridge = nullptr;
        tl_bridge = this;

        JSValue global_obj = JS_GetGlobalObject(ctx);
        JSValue console_obj = JS_NewObject(ctx);
        JSValue log_fn = JS_NewCFunction(ctx, [](JSContext *c, JSValueConst,
                                                    int argc, JSValueConst *argv) -> JSValue {
            std::string output;
            for (int i = 0; i < argc; i++) {
                if (i > 0) output += " ";
                const char *s = JS_ToCString(c, argv[i]);
                if (s) { output += s; JS_FreeCString(c, s); }
            }
            output += "\n";
            if (tl_bridge) {
                cJSON *body = cJSON_CreateObject();
                cJSON_AddStringToObject(body, "category", "console");
                cJSON_AddStringToObject(body, "output", output.c_str());
                tl_bridge->pushEvent("output", body);
            }
            return JS_UNDEFINED;
        }, "log", 1);
        JS_SetPropertyStr(ctx, console_obj, "log", log_fn);
        JS_SetPropertyStr(ctx, global_obj, "console", console_obj);
        JS_FreeValue(ctx, global_obj);
    }

    /* Stop on entry */
    if (stopOnEntry) {
        JS_DebugPause(rt);
    }

    /* Fire loadedSource event */
    {
        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "reason", "new");
        cJSON *src = cJSON_CreateObject();
        cJSON_AddStringToObject(src, "name", scriptPath.c_str());
        cJSON_AddStringToObject(src, "path", scriptPath.c_str());
        cJSON_AddNumberToObject(src, "sourceReference", 0);
        cJSON_AddItemToObject(body, "source", src);
        pushEvent("loadedSource", body);
    }

    /* Evaluate script as ES module with debug info.
       In noix-engine, all scripts are ES modules. */
    JSValue mod_val = JS_Eval(ctx, source.c_str(), source.size(),
                               scriptPath.c_str(),
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY | JS_EVAL_FLAG_DEBUG_INFO);
    JSValue result;
    if (!JS_IsException(mod_val)) {
        /* Set import.meta for the main module */
        dap_set_import_meta(ctx, mod_val, true);

        /* Apply pending breakpoints now that the script is compiled and
           registered with QuickJS. This resolves filenames to match
           QuickJS internal names. */
        {
            JSDebugScriptInfo *scripts = nullptr;
            int scriptCount = JS_DebugGetLoadedScripts(rt, &scripts);
            for (auto &pb : pendingBreakpoints) {
                std::string resolvedPath = pb.filename;
                std::string normPb = normalizePath(pb.filename.c_str());
                for (int si = 0; si < scriptCount; si++) {
                    if (normalizePath(scripts[si].filename) == normPb) {
                        resolvedPath = scripts[si].filename;
                        break;
                    }
                }
                uint32_t id;
                if (!pb.condition.empty()) {
                    id = JS_DebugSetConditionalBreakpoint(rt, resolvedPath.c_str(),
                                                           pb.line, pb.condition.c_str());
                } else {
                    id = JS_DebugSetBreakpoint(rt, resolvedPath.c_str(), pb.line);
                }
                if (id > 0) {
                    Breakpoint b;
                    b.id = id;
                    b.filename = resolvedPath;
                    b.line = pb.line;
                    b.condition = pb.condition;
                    b.verified = true;
                    breakpoints.push_back(b);
                }
            }
            if (scripts) JS_DebugFreeScriptInfo(rt, scripts, scriptCount);
            pendingBreakpoints.clear();
        }

        /* Evaluate the module -- JS_EvalFunction executes the module and
           returns a Promise for async modules. */
        result = JS_EvalFunction(ctx, mod_val);
    } else {
        result = mod_val;
    }

    /* Execute pending jobs (module imports resolve via promise jobs) */
    for (;;) {
        JSContext *ctx1;
        int ret = JS_ExecutePendingJob(rt, &ctx1);
        if (ret <= 0) break;
    }

    if (JS_IsException(result)) {
        JSValue exc = JS_GetException(ctx);
        const char *str = JS_ToCString(ctx, exc);
        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "category", "stderr");
        cJSON_AddStringToObject(body, "output", str ? str : "Unknown error");
        pushEvent("output", body);
        JS_FreeCString(ctx, str);
        JS_FreeValue(ctx, exc);
    }

    JS_FreeValue(ctx, result);

    /* Script finished — but do NOT send "terminated" here.
       The script thread is persistent (job loop), and the main program may
       invoke script callbacks at any time. The DAP session stays alive until
       the client disconnects or the engine shuts down. */
    running = false;

    /* Push resume event so game loop un-freezes after script ends */
    pushResumeEvent();
}

} // namespace noix::debug
