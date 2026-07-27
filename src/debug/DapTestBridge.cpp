/*
 * DapTestBridge — Minimal DAP debug bridge for QuickJS.
 *
 * Reads DAP requests from stdin or TCP socket, dispatches to QuickJS debug API,
 * writes DAP responses/events to stdout or TCP socket.
 *
 * Transport modes:
 *   stdio (default)  —  for VS Code launch mode (DebugAdapterExecutable)
 *   TCP (--port N)   —  for VS Code attach mode (DebugAdapterServer)
 *
 * Supported DAP requests:
 *   initialize, launch, disconnect, setBreakpoints,
 *   setExceptionBreakpoints, continue, next, stepIn, stepOut,
 *   pause, stackTrace, scopes, variables, evaluate, threads,
 *   loadedSources
 *
 * Build: linked against quickjs (qjs) + cJSON + SDL3_net
 */

#include "debug/DapTestBridge.h"
#include "cJSON.h"

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

/* ---- Global shutdown flag (accessible before DapBridge definition) ---- */
static std::atomic<bool> g_shuttingDown{false};

/* ---- SDL_net for TCP transport ---- */
#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>

/* ---- DAP transport abstraction ---- */

struct DapTransport {
    /* Read a single byte. Returns -1 on EOF/error. */
    int (*readByte)(void *ctx);
    /* Write a complete DAP message (Content-Length + body). */
    void (*writeMessage)(void *ctx, const std::string &msg);
    void *ctx;
};

/* ---- Stdio transport ---- */

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

static void init_stdio_binary() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    /* Disable Windows abort() popup — just terminate silently */
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

static void init_stdio_transport(DapTransport *t) {
    t->readByte = stdio_read_byte;
    t->writeMessage = stdio_write_message;
    t->ctx = nullptr;
}

/* ---- TCP transport ---- */

struct TcpCtx {
    NET_Server *server = nullptr;
    NET_StreamSocket *client = nullptr;
    std::string recvBuffer;
    int port = 0;
};

static int tcp_read_byte(void *ctx) {
    auto *t = static_cast<TcpCtx *>(ctx);
    while (t->recvBuffer.empty()) {
        if (!t->client || g_shuttingDown.load()) return -1;
        /* Wait up to 500ms for data; timeout is NOT an error — just retry.
           This keeps the connection alive while the user is idle (e.g. between
           clicking Step Over / Continue in VS Code). */
        if (!NET_WaitUntilInputAvailable(reinterpret_cast<void **>(&t->client), 1, 500)) {
            if (!t->client || g_shuttingDown.load()) return -1;
            continue; /* Timeout — no data yet, retry */
        }
        if (!t->client || g_shuttingDown.load()) return -1;
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

static void tcp_write_message(void *ctx, const std::string &msg) {
    auto *t = static_cast<TcpCtx *>(ctx);
    if (!t->client) return;
    NET_WriteToStreamSocket(t->client, msg.data(), static_cast<int>(msg.size()));
    NET_WaitUntilStreamSocketDrained(t->client, 500);
}

static bool init_tcp_transport(DapTransport *t, TcpCtx *tcp, int port) {
    tcp->port = port;
    tcp->server = nullptr;
    tcp->client = nullptr;
    tcp->recvBuffer.clear();

    if (!NET_Init()) {
        fprintf(stderr, "NET_Init failed: %s\n", SDL_GetError());
        return false;
    }

    tcp->server = NET_CreateServer(nullptr, port, 0);
    if (!tcp->server) {
        fprintf(stderr, "NET_CreateServer failed: %s\n", SDL_GetError());
        NET_Quit();
        return false;
    }

    fprintf(stderr, "DAP bridge listening on port %d, waiting for connection...\n", port);

    /* Wait for a single client connection (DAP is 1:1) */
    while (!tcp->client) {
        NET_AcceptClient(tcp->server, &tcp->client);
        if (!tcp->client) {
            SDL_Delay(50);
        }
    }

    fprintf(stderr, "Client connected on port %d\n", port);

    t->readByte = tcp_read_byte;
    t->writeMessage = tcp_write_message;
    t->ctx = tcp;
    return true;
}

static void cleanup_tcp(TcpCtx *tcp) {
    if (tcp->client) {
        NET_DestroyStreamSocket(tcp->client);
        tcp->client = nullptr;
    }
    if (tcp->server) {
        NET_DestroyServer(tcp->server);
        tcp->server = nullptr;
    }
    NET_Quit();
}

/* ---- DAP wire protocol helpers ---- */

static bool dap_read_message(DapTransport &transport, std::string &out) {
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

static void dap_write_message(DapTransport &transport, std::mutex &writeMutex,
                               const std::string &json) {
    std::lock_guard<std::mutex> lk(writeMutex);
    std::string msg = "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n" + json;
    transport.writeMessage(transport.ctx, msg);
}

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

/* ---- Bridge state ---- */

struct DapBridge {
    JSRuntime *rt = nullptr;
    JSContext *ctx = nullptr;
    int seq = 1;
    bool initialized = false;
    bool launched = false;
    std::atomic<bool> running{false};
    std::atomic<bool> configDone{false};
    std::atomic<bool> firstStop{true}; /* first stop = "entry" reason per DAP convention */
    std::string scriptPath;
    bool stopOnEntry = false;

    /* Pending stopped event — buffered until configurationDone is received.
       DAP spec: stopped event should only be sent after configurationDone. */
    struct PendingStop {
        std::string reason;
        int threadId = 0;
        int line = 0;
        int column = 0;
        uint32_t bp_id = 0;
        bool valid = false;
    };
    PendingStop pendingStop;

    /* transport (stdio or TCP) */
    DapTransport transport;
    std::mutex writeMutex;

    /* thread management */
    std::thread scriptThread;
    std::thread handlerThread;

    /* command queue: main thread -> script thread */
    std::mutex cmdMutex;
    std::condition_variable cmdCv;
    std::queue<std::function<void()>> cmdQueue;

    /* event queue: script thread -> main thread */
    std::mutex evtMutex;
    std::vector<std::string> evtQueue;

    /* async request queue: reader thread -> handler thread */
    std::mutex reqMutex;
    std::condition_variable reqCv;
    std::queue<std::string> reqQueue;

    /* breakpoint tracking */
    struct Breakpoint {
        uint32_t id;
        std::string filename;
        int line;
        std::string condition;
        bool verified;
    };
    std::vector<Breakpoint> breakpoints;

    /* pending breakpoints (set before launch) */
    struct PendingBreakpoint {
        std::string filename;
        int line;
        std::string condition;
    };
    std::vector<PendingBreakpoint> pendingBreakpoints;

    /* pending exception filter state */
    int pendingExceptionState = 0;

    /* object expansion: stores JSValues for variablesReference lookup */
    struct ObjectRef {
        int varRef;         /* the variablesReference that refers to this object */
        JSValue obj;        /* the JS object value */
    };
    std::vector<ObjectRef> objectRefs;
    int nextObjectVarRef = 100000; /* start high to avoid collision with scope refs */

    int addObjectRef(JSValue obj) {
        int ref = nextObjectVarRef++;
        objectRefs.push_back({ref, obj});
        return ref;
    }

    JSValue findObjectRef(int varRef) {
        for (auto &or_ref : objectRefs) {
            if (or_ref.varRef == varRef)
                return or_ref.obj;
        }
        return JS_UNDEFINED;
    }

    void clearObjectRefs() {
        for (auto &or_ref : objectRefs) {
            JS_FreeValue(ctx, or_ref.obj);
        }
        objectRefs.clear();
        nextObjectVarRef = 100000;
    }

    void pushEvent(const std::string &eventType, cJSON *body) {
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddNumberToObject(msg, "seq", seq++);
        cJSON_AddStringToObject(msg, "type", "event");
        cJSON_AddStringToObject(msg, "event", eventType.c_str());
        if (body) cJSON_AddItemToObject(msg, "body", body);
        char *s = cJSON_PrintUnformatted(msg);
        std::string json(s);
        cJSON_free(s);
        cJSON_Delete(msg);
        fprintf(stderr, "[DAP] <<< event: %s %s\n", eventType.c_str(), json.c_str());
        /* Write event immediately — it may be produced on the script thread
           while the main thread is blocked reading stdin. */
        dap_write_message(transport, writeMutex, json);
    }

    void flushEvents() {
        /* Events are now written immediately in pushEvent. */
    }

    void sendResponse(int requestSeq, const char *command, bool success,
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
        fprintf(stderr, "[DAP] <<< response: %s\n", json.c_str());
        dap_write_message(transport, writeMutex, json);
        flushEvents();
    }

    /* Enqueue a command for the script thread and wait for completion.
       Must be called from the main (DAP reader) thread.
       The script thread executes the command during drainQueue. */
    void enqueueAndWait(std::function<void()> fn) {
        std::mutex waitMutex;
        std::condition_variable waitCv;
        bool done = false;

        {
            std::lock_guard<std::mutex> lk(cmdMutex);
            cmdQueue.push([&]() {
                fn();
                {
                    std::lock_guard<std::mutex> wl(waitMutex);
                    done = true;
                }
                waitCv.notify_one();
            });
        }

        /* Wait for the script thread to execute the command.
           This works because drainQueue is called in the paused loop
           on the script thread, which will pick up our command. */
        std::unique_lock<std::mutex> wl(waitMutex);
        waitCv.wait(wl, [&]() { return done; });
    }

    /* drain: called on script thread while paused */
    static void drainQueue(void *opaque) {
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
            fprintf(stderr, "[DAP] drainQueue: executing command, queue remaining=%zu\n", self->cmdQueue.size());
            fn();
            fprintf(stderr, "[DAP] drainQueue: command done, debug_state=%d\n",
                    self->rt ? JS_DebugGetState(self->rt) : -1);
        }
    }

    /* debug callback: called on script thread */
    static void debugCallback(JSRuntime *rt, JSDebugEventType event,
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

        fprintf(stderr, "[DAP] debugCallback: event=%d reason=%s file=%s line=%d col=%d bp_id=%u debug_state=%d configDone=%d\n",
                event, reason, filename ? filename : "(null)", line, col, bp_id,
                rt ? JS_DebugGetState(rt) : -1, self->configDone.load());

        if (!self->configDone.load()) {
            /* Buffer the stopped event — send it after configurationDone */
            fprintf(stderr, "[DAP] debugCallback: buffering stopped event (configDone not yet received)\n");
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
    }
};

static DapBridge g_bridge;

/* ---- Minimal module support (no quickjs-libc dependency) ---- */

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

/* ---- Module loader with DEBUG_INFO ---- */

/* Custom module loader that compiles modules with JS_EVAL_FLAG_DEBUG_INFO
   so that breakpoints work in imported modules. */
static JSModuleDef *dap_module_loader(JSContext *ctx, const char *module_name,
                                       void *opaque) {
    /* Resolve relative module paths against the main script directory.
       QuickJS default normalizer strips './' but doesn't handle Windows
       backslash paths, so we get a bare filename like 'dap_multifile_mod.js'. */
    std::string resolved_path;
    if (module_name[0] != '/' && module_name[0] != '\\' &&
        !(module_name[0] && module_name[1] == ':')) {
        /* Relative path — resolve against main script directory */
        std::string base_dir = g_bridge.scriptPath;
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
        g_bridge.pushEvent("loadedSource", body);
    }

    /* The module is already referenced by the module system, free our ref */
    JS_FreeValue(ctx, val);
    return m;
}


/* ---- DAP request handlers ---- */

/* Convert a potentially relative path to absolute, using CWD.
   Returns a thread-local static buffer — use immediately or copy. */
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
    if (abs.size() >= 2 && abs[1] == ':') abs[0] = tolower(abs[0]);
    return abs;
}

static void handleInitialize(cJSON *args, int requestSeq) {
    g_bridge.initialized = true;

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

    g_bridge.sendResponse(requestSeq, "initialize", true, nullptr, body);

    /* DAP protocol requires an 'initialized' event after the initialize response */
    g_bridge.pushEvent("initialized", cJSON_CreateObject());
    g_bridge.flushEvents();
}

static void handleLaunch(cJSON *args, int requestSeq, const char *commandName = "launch") {
    const char *script = json_get_str(args, "script");
    if (script && script[0]) g_bridge.scriptPath = normalizePath(script);
    g_bridge.stopOnEntry = json_get_bool(args, "stopOnEntry", false);

    if (g_bridge.scriptPath.empty()) {
        g_bridge.sendResponse(requestSeq, commandName, false, "no script path", nullptr);
        return;
    }

    g_bridge.launched = true;
    g_bridge.running = true;
    g_bridge.firstStop = true;

    /* Start script thread */
    g_bridge.scriptThread = std::thread([]() {
        /* Read script file */
        std::ifstream file(g_bridge.scriptPath);
        if (!file.is_open()) {
            cJSON *body = cJSON_CreateObject();
            cJSON_AddStringToObject(body, "category", "stderr");
            cJSON_AddStringToObject(body, "output", "Cannot open script file");
            g_bridge.pushEvent("output", body);
            g_bridge.running = false;
            return;
        }
        std::stringstream ss;
        ss << file.rdbuf();
        std::string source = ss.str();

        /* Create runtime and context */
        g_bridge.rt = JS_NewRuntime();
        g_bridge.ctx = JS_NewContext(g_bridge.rt);

        /* Set debug callbacks */
        JS_SetDebugCallback(g_bridge.rt, DapBridge::debugCallback, &g_bridge);
        JS_SetDebugDrainQueue(g_bridge.rt, DapBridge::drainQueue);

        /* Set module loader — custom loader adds JS_EVAL_FLAG_DEBUG_INFO
           so breakpoints work in imported modules */
        JS_SetModuleLoaderFunc(g_bridge.rt, nullptr, dap_module_loader, nullptr);

        /* Apply pending exception breakpoint state */
        if (g_bridge.pendingExceptionState > 0) {
            JS_DebugSetPauseOnExceptions(g_bridge.rt, g_bridge.pendingExceptionState);
        }

        /* Install console API — redirect console.log to DAP output events */
        {
            JSValue global_obj = JS_GetGlobalObject(g_bridge.ctx);
            JSValue console_obj = JS_NewObject(g_bridge.ctx);
            JSValue log_fn = JS_NewCFunction(g_bridge.ctx, [](JSContext *ctx, JSValueConst this_val,
                                                                int argc, JSValueConst *argv) -> JSValue {
                std::string output;
                for (int i = 0; i < argc; i++) {
                    if (i > 0) output += " ";
                    const char *s = JS_ToCString(ctx, argv[i]);
                    if (s) { output += s; JS_FreeCString(ctx, s); }
                }
                output += "\n";
                cJSON *body = cJSON_CreateObject();
                cJSON_AddStringToObject(body, "category", "console");
                cJSON_AddStringToObject(body, "output", output.c_str());
                g_bridge.pushEvent("output", body);
                return JS_UNDEFINED;
            }, "log", 1);
            JS_SetPropertyStr(g_bridge.ctx, console_obj, "log", log_fn);
            JS_SetPropertyStr(g_bridge.ctx, global_obj, "console", console_obj);
            JS_FreeValue(g_bridge.ctx, global_obj);
        }

        /* Stop on entry */
        if (g_bridge.stopOnEntry) {
            JS_DebugPause(g_bridge.rt);
        }

        /* Fire loadedSource event */
        {
            cJSON *body = cJSON_CreateObject();
            cJSON_AddStringToObject(body, "reason", "new");
            cJSON *src = cJSON_CreateObject();
            cJSON_AddStringToObject(src, "name", g_bridge.scriptPath.c_str());
            cJSON_AddStringToObject(src, "path", g_bridge.scriptPath.c_str());
            cJSON_AddNumberToObject(src, "sourceReference", 0);
            cJSON_AddItemToObject(body, "source", src);
            g_bridge.pushEvent("loadedSource", body);
        }

        /* Evaluate script as ES module with debug info.
           In noix-engine, all scripts are ES modules. */
        JSValue mod_val = JS_Eval(g_bridge.ctx, source.c_str(), source.size(),
                                   g_bridge.scriptPath.c_str(),
                                   JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY | JS_EVAL_FLAG_DEBUG_INFO);
        JSValue result;
        if (!JS_IsException(mod_val)) {
            /* Set import.meta for the main module */
            dap_set_import_meta(g_bridge.ctx, mod_val, true);

            /* Apply pending breakpoints now that the script is compiled and
               registered with QuickJS. This resolves filenames to match
               QuickJS internal names. */
            {
                JSDebugScriptInfo *scripts = nullptr;
                int scriptCount = JS_DebugGetLoadedScripts(g_bridge.rt, &scripts);
                for (auto &pb : g_bridge.pendingBreakpoints) {
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
                        id = JS_DebugSetConditionalBreakpoint(g_bridge.rt, resolvedPath.c_str(),
                                                               pb.line, pb.condition.c_str());
                    } else {
                        id = JS_DebugSetBreakpoint(g_bridge.rt, resolvedPath.c_str(), pb.line);
                    }
                    if (id > 0) {
                        DapBridge::Breakpoint b;
                        b.id = id;
                        b.filename = resolvedPath;
                        b.line = pb.line;
                        b.condition = pb.condition;
                        b.verified = true;
                        g_bridge.breakpoints.push_back(b);
                    }
                }
                if (scripts) JS_DebugFreeScriptInfo(g_bridge.rt, scripts, scriptCount);
                g_bridge.pendingBreakpoints.clear();
            }

            /* Evaluate the module — JS_EvalFunction executes the module and
               returns a Promise for async modules. */
            result = JS_EvalFunction(g_bridge.ctx, mod_val);
        } else {
            result = mod_val;
        }

        /* Execute pending jobs (module imports resolve via promise jobs) */
        for (;;) {
            JSContext *ctx1;
            int ret = JS_ExecutePendingJob(g_bridge.rt, &ctx1);
            if (ret <= 0) break;
        }

        if (JS_IsException(result)) {
            JSValue exc = JS_GetException(g_bridge.ctx);
            const char *str = JS_ToCString(g_bridge.ctx, exc);
            cJSON *body = cJSON_CreateObject();
            cJSON_AddStringToObject(body, "category", "stderr");
            cJSON_AddStringToObject(body, "output", str ? str : "Unknown error");
            g_bridge.pushEvent("output", body);
            JS_FreeCString(g_bridge.ctx, str);
            JS_FreeValue(g_bridge.ctx, exc);
        }

        JS_FreeValue(g_bridge.ctx, result);

        /* Script finished */
        g_bridge.pushEvent("terminated", cJSON_CreateObject());
        g_bridge.running = false;

        /* Clear breakpoints and object refs before freeing context/runtime */
        JS_DebugClearBreakpoints(g_bridge.rt);
        g_bridge.clearObjectRefs();

        JS_FreeContext(g_bridge.ctx);
        JS_RunGC(g_bridge.rt);
        JS_FreeRuntime(g_bridge.rt);
        g_bridge.ctx = nullptr;
        g_bridge.rt = nullptr;
    });

    g_bridge.sendResponse(requestSeq, commandName, true, nullptr, nullptr);
}

static void handleSetBreakpoints(cJSON *args, int requestSeq) {
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

    /* Results array — filled during script-thread execution or for pending */
    cJSON *result = cJSON_CreateArray();

    /* Normalize the client-provided path for comparison */
    std::string normPath = normalizePath(path);

    /* Helper: check if a breakpoint filename matches the client path */
    auto pathMatches = [&](const std::string &bpFilename) -> bool {
        return normalizePath(bpFilename.c_str()) == normPath;
    };

    if (g_bridge.rt && g_bridge.running) {
        /* Runtime exists and script is running — do everything on the script thread
           to avoid race conditions and ensure breakpoint line correction works */
        g_bridge.enqueueAndWait([&]() {
            /* Resolve client path to QuickJS internal filename */
            std::string resolvedPath(path);
            JSDebugScriptInfo *scripts = nullptr;
            int scriptCount = JS_DebugGetLoadedScripts(g_bridge.rt, &scripts);
            for (int si = 0; si < scriptCount; si++) {
                if (normalizePath(scripts[si].filename) == normPath) {
                    resolvedPath = scripts[si].filename;
                    break;
                }
            }
            if (scripts) JS_DebugFreeScriptInfo(g_bridge.rt, scripts, scriptCount);

            /* Remove existing breakpoints for this file */
            for (auto it = g_bridge.breakpoints.begin(); it != g_bridge.breakpoints.end(); ) {
                if (pathMatches(it->filename)) {
                    JS_DebugRemoveBreakpoint(g_bridge.rt, it->id);
                    it = g_bridge.breakpoints.erase(it);
                } else {
                    ++it;
                }
            }

            /* Set new breakpoints */
            for (auto &req : requestedBps) {
                uint32_t id;
                if (!req.condition.empty()) {
                    id = JS_DebugSetConditionalBreakpoint(g_bridge.rt, resolvedPath.c_str(), req.line, req.condition.c_str());
                } else {
                    id = JS_DebugSetBreakpoint(g_bridge.rt, resolvedPath.c_str(), req.line);
                }

                if (id > 0) {
                    DapBridge::Breakpoint b;
                    b.id = id;
                    b.filename = resolvedPath;
                    b.line = req.line;
                    b.condition = req.condition;
                    b.verified = true;
                    g_bridge.breakpoints.push_back(b);

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
    } else if (g_bridge.rt) {
        /* Runtime exists but not running — set directly on main thread */
        /* Resolve client path to QuickJS internal filename */
        JSDebugScriptInfo *scripts = nullptr;
        int scriptCount = JS_DebugGetLoadedScripts(g_bridge.rt, &scripts);
        std::string resolvedPath(path);
        for (int si = 0; si < scriptCount; si++) {
            if (normalizePath(scripts[si].filename) == normPath) {
                resolvedPath = scripts[si].filename;
                break;
            }
        }
        if (scripts) JS_DebugFreeScriptInfo(g_bridge.rt, scripts, scriptCount);

        for (auto it = g_bridge.breakpoints.begin(); it != g_bridge.breakpoints.end(); ) {
            if (pathMatches(it->filename)) {
                JS_DebugRemoveBreakpoint(g_bridge.rt, it->id);
                it = g_bridge.breakpoints.erase(it);
            } else {
                ++it;
            }
        }
        for (auto &req : requestedBps) {
            uint32_t id;
            if (!req.condition.empty()) {
                id = JS_DebugSetConditionalBreakpoint(g_bridge.rt, resolvedPath.c_str(), req.line, req.condition.c_str());
            } else {
                id = JS_DebugSetBreakpoint(g_bridge.rt, resolvedPath.c_str(), req.line);
            }
            if (id > 0) {
                DapBridge::Breakpoint b;
                b.id = id;
                b.filename = resolvedPath;
                b.line = req.line;
                b.condition = req.condition;
                b.verified = true;
                g_bridge.breakpoints.push_back(b);

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
        /* No runtime yet — save as pending */
        for (auto it = g_bridge.breakpoints.begin(); it != g_bridge.breakpoints.end(); ) {
            if (pathMatches(it->filename))
                it = g_bridge.breakpoints.erase(it);
            else
                ++it;
        }
        for (auto it = g_bridge.pendingBreakpoints.begin(); it != g_bridge.pendingBreakpoints.end(); ) {
            if (pathMatches(it->filename))
                it = g_bridge.pendingBreakpoints.erase(it);
            else
                ++it;
        }
        for (auto &req : requestedBps) {
            DapBridge::PendingBreakpoint pb;
            pb.filename = path;
            pb.line = req.line;
            pb.condition = req.condition;
            g_bridge.pendingBreakpoints.push_back(pb);

            cJSON *r = cJSON_CreateObject();
            cJSON_AddBoolToObject(r, "verified", true);
            cJSON_AddNumberToObject(r, "line", req.line);
            cJSON_AddItemToArray(result, r);
        }
    }

    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "breakpoints", result);
    g_bridge.sendResponse(requestSeq, "setBreakpoints", true, nullptr, body);
}

static void handleSetExceptionBreakpoints(cJSON *args, int requestSeq) {
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

    g_bridge.pendingExceptionState = state;

    if (g_bridge.rt) {
        JS_DebugSetPauseOnExceptions(g_bridge.rt, state);
    }

    g_bridge.sendResponse(requestSeq, "setExceptionBreakpoints", true, nullptr, nullptr);
}

static void handleContinue(int requestSeq) {
    fprintf(stderr, "[DAP] handleContinue: rt=%p\n", (void*)g_bridge.rt);
    g_bridge.enqueueAndWait([]() {
        fprintf(stderr, "[DAP] enqueueAndWait: calling JS_DebugContinue, rt=%p\n", (void*)g_bridge.rt);
        JS_DebugContinue(g_bridge.rt);
    });
    fprintf(stderr, "[DAP] handleContinue: done\n");
    cJSON *body = cJSON_CreateObject();
    cJSON_AddBoolToObject(body, "allThreadsContinued", true);
    g_bridge.sendResponse(requestSeq, "continue", true, nullptr, body);
}

static void handleNext(int requestSeq) {
    fprintf(stderr, "[DAP] handleNext: enter, rt=%p, debug_state=%d\n",
            (void*)g_bridge.rt, g_bridge.rt ? JS_DebugGetState(g_bridge.rt) : -1);
    g_bridge.enqueueAndWait([]() {
        fprintf(stderr, "[DAP] handleNext: enqueueAndWait executing, calling JS_DebugStep\n");
        JS_DebugStep(g_bridge.rt, 1); /* step over */
        fprintf(stderr, "[DAP] handleNext: JS_DebugStep done, debug_state=%d\n",
                JS_DebugGetState(g_bridge.rt));
    });
    fprintf(stderr, "[DAP] handleNext: enqueueAndWait returned\n");
    g_bridge.sendResponse(requestSeq, "next", true, nullptr, nullptr);
    fprintf(stderr, "[DAP] handleNext: response sent\n");
}

static void handleStepIn(int requestSeq) {
    fprintf(stderr, "[DAP] handleStepIn: enter, rt=%p, debug_state=%d\n",
            (void*)g_bridge.rt, g_bridge.rt ? JS_DebugGetState(g_bridge.rt) : -1);
    g_bridge.enqueueAndWait([]() {
        JS_DebugStep(g_bridge.rt, 0); /* step into */
    });
    g_bridge.sendResponse(requestSeq, "stepIn", true, nullptr, nullptr);
}

static void handleStepOut(int requestSeq) {
    fprintf(stderr, "[DAP] handleStepOut: enter, rt=%p, debug_state=%d\n",
            (void*)g_bridge.rt, g_bridge.rt ? JS_DebugGetState(g_bridge.rt) : -1);
    g_bridge.enqueueAndWait([]() {
        JS_DebugStep(g_bridge.rt, 2); /* step out */
    });
    g_bridge.sendResponse(requestSeq, "stepOut", true, nullptr, nullptr);
}

static void handlePause(int requestSeq) {
    if (g_bridge.rt) {
        JS_DebugPause(g_bridge.rt);
    }
    g_bridge.sendResponse(requestSeq, "pause", true, nullptr, nullptr);
}

static void handleStackTrace(cJSON *args, int requestSeq) {
    int startFrame = json_get_int(args, "startFrame", 0);
    int levels = json_get_int(args, "levels", 0);

    JSDebugFrameInfo *frames = nullptr;
    int count = 0;

    g_bridge.enqueueAndWait([&]() {
        count = JS_DebugCaptureStack(g_bridge.rt, &frames);
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

    if (frames) JS_DebugFreeFrameInfo(g_bridge.rt, frames, count);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "stackFrames", stackFrames);
    cJSON_AddNumberToObject(body, "totalFrames", count);
    g_bridge.sendResponse(requestSeq, "stackTrace", true, nullptr, body);
}

/* Scope varRef encoding:
   Local scope:    frameId * 100 + 1
   Closure scope:  frameId * 100 + 2  (only if closure vars exist)
   Global scope:   frameId * 100 + 3  (only if global scope present)
   Object expand:  >= 100000           (handled separately)
*/
static const int SCOPE_REF_LOCAL    = 1;
static const int SCOPE_REF_CLOSURE  = 2;
static const int SCOPE_REF_GLOBAL   = 3;

static void handleScopes(cJSON *args, int requestSeq) {
    int frameId = json_get_int(args, "frameId", 0);

    JSDebugScopeInfo *scopes = nullptr;
    int scopeCount = 0;

    g_bridge.enqueueAndWait([&]() {
        scopeCount = JS_DebugGetFrameScopes(g_bridge.rt, frameId, &scopes);
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

    if (scopes) JS_DebugFreeScopeInfo(g_bridge.rt, scopes, scopeCount);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "scopes", scopesArr);
    g_bridge.sendResponse(requestSeq, "scopes", true, nullptr, body);
}

static void formatJSValue(cJSON *v, JSValue val, const char *valueKey = "value") {
    if (JS_IsNumber(val)) {
        double d;
        JS_ToFloat64(g_bridge.ctx, &d, val);
        char buf[64];
        snprintf(buf, sizeof(buf), "%g", d);
        cJSON_AddStringToObject(v, valueKey, buf);
        cJSON_AddStringToObject(v, "type", "number");
    } else if (JS_IsBool(val)) {
        cJSON_AddStringToObject(v, valueKey, JS_ToBool(g_bridge.ctx, val) ? "true" : "false");
        cJSON_AddStringToObject(v, "type", "boolean");
    } else if (JS_IsString(val)) {
        const char *s = JS_ToCString(g_bridge.ctx, val);
        cJSON_AddStringToObject(v, valueKey, s ? s : "\"\"");
        cJSON_AddStringToObject(v, "type", "string");
        JS_FreeCString(g_bridge.ctx, s);
    } else if (JS_IsNull(val)) {
        cJSON_AddStringToObject(v, valueKey, "null");
        cJSON_AddStringToObject(v, "type", "null");
    } else if (JS_IsUndefined(val)) {
        cJSON_AddStringToObject(v, valueKey, "undefined");
        cJSON_AddStringToObject(v, "type", "undefined");
    } else if (JS_VALUE_GET_TAG(val) == JS_TAG_UNINITIALIZED) {
        /* TDZ — let/const variable not yet initialized */
        cJSON_AddStringToObject(v, valueKey, "<uninitialized>");
        cJSON_AddStringToObject(v, "type", "undefined");
    } else if (JS_IsObject(val)) {
        /* Check for specific object types */
        if (JS_IsArray(val)) {
            /* Get array length */
            JSAtom lengthAtom = JS_NewAtom(g_bridge.ctx, "length");
            JSValue lenVal = JS_GetProperty(g_bridge.ctx, val, lengthAtom);
            uint32_t len = 0;
            if (JS_IsNumber(lenVal)) {
                int32_t i32;
                if (JS_ToInt32(g_bridge.ctx, &i32, lenVal) == 0)
                    len = (uint32_t)i32;
            }
            JS_FreeValue(g_bridge.ctx, lenVal);
            JS_FreeAtom(g_bridge.ctx, lengthAtom);
            char buf[64];
            snprintf(buf, sizeof(buf), "Array(%u)", len);
            cJSON_AddStringToObject(v, valueKey, buf);
            cJSON_AddStringToObject(v, "type", "object");
        } else if (JS_IsFunction(g_bridge.ctx, val)) {
            cJSON_AddStringToObject(v, valueKey, "function");
            cJSON_AddStringToObject(v, "type", "function");
        } else {
            cJSON_AddStringToObject(v, valueKey, "Object");
            cJSON_AddStringToObject(v, "type", "object");
        }
        int objRef = g_bridge.addObjectRef(JS_DupValue(g_bridge.ctx, val));
        cJSON_AddNumberToObject(v, "variablesReference", objRef);
    } else {
        cJSON_AddStringToObject(v, valueKey, "[unknown]");
    }
}

static void handleVariables(cJSON *args, int requestSeq) {
    int varRef = json_get_int(args, "variablesReference", 0);

    /* Object expansion: varRef >= 100000 */
    if (varRef >= 100000) {
        JSValue obj = g_bridge.findObjectRef(varRef);
        cJSON *varsArr = cJSON_CreateArray();

        if (JS_IsObject(obj)) {
            g_bridge.enqueueAndWait([&]() {
                JSPropertyEnum *props = nullptr;
                uint32_t propCount = 0;
                JS_GetOwnPropertyNames(g_bridge.ctx, &props, &propCount, obj,
                                       JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY);
                for (uint32_t i = 0; i < propCount; i++) {
                    const char *name = JS_AtomToCString(g_bridge.ctx, props[i].atom);
                    JSValue propVal = JS_GetProperty(g_bridge.ctx, obj, props[i].atom);

                    cJSON *v = cJSON_CreateObject();
                    cJSON_AddStringToObject(v, "name", name ? name : "");
                    formatJSValue(v, propVal);
                    if (!JS_IsObject(propVal)) {
                        cJSON_AddNumberToObject(v, "variablesReference", 0);
                    }
                    cJSON_AddItemToArray(varsArr, v);

                    JS_FreeCString(g_bridge.ctx, name);
                    JS_FreeValue(g_bridge.ctx, propVal);
                    JS_FreeAtom(g_bridge.ctx, props[i].atom);
                }
                js_free(g_bridge.ctx, props);
            });
        }

        cJSON *body = cJSON_CreateObject();
        cJSON_AddItemToObject(body, "variables", varsArr);
        g_bridge.sendResponse(requestSeq, "variables", true, nullptr, body);
        return;
    }

    /* Decode frameId and scope type from varRef.
       Encoding: frameId * 100 + SCOPE_REF_{LOCAL,CLOSURE,GLOBAL} */
    int frameId = varRef / 100;
    int scopeType = varRef % 100;

    if (scopeType == SCOPE_REF_GLOBAL) {
        /* Enumerate global object properties */
        cJSON *varsArr = cJSON_CreateArray();
        g_bridge.enqueueAndWait([&]() {
            JSValue globalObj = JS_GetGlobalObject(g_bridge.ctx);
            JSPropertyEnum *props = nullptr;
            uint32_t propCount = 0;
            JS_GetOwnPropertyNames(g_bridge.ctx, &props, &propCount, globalObj,
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY);
            for (uint32_t i = 0; i < propCount; i++) {
                const char *name = JS_AtomToCString(g_bridge.ctx, props[i].atom);
                JSValue propVal = JS_GetProperty(g_bridge.ctx, globalObj, props[i].atom);

                cJSON *v = cJSON_CreateObject();
                cJSON_AddStringToObject(v, "name", name ? name : "");
                formatJSValue(v, propVal);
                if (!JS_IsObject(propVal)) {
                    cJSON_AddNumberToObject(v, "variablesReference", 0);
                }
                cJSON_AddItemToArray(varsArr, v);

                JS_FreeCString(g_bridge.ctx, name);
                JS_FreeValue(g_bridge.ctx, propVal);
                JS_FreeAtom(g_bridge.ctx, props[i].atom);
            }
            js_free(g_bridge.ctx, props);
            JS_FreeValue(g_bridge.ctx, globalObj);
        });
        cJSON *body = cJSON_CreateObject();
        cJSON_AddItemToObject(body, "variables", varsArr);
        g_bridge.sendResponse(requestSeq, "variables", true, nullptr, body);
        return;
    }

    if (scopeType == SCOPE_REF_CLOSURE) {
        /* Closure scope: collect variables from enclosing frames.
           Walk the call stack from outermost to the frame just above the
           current one, collecting their locals. Inner frame vars override
           outer ones (same name). */
        cJSON *varsArr = cJSON_CreateArray();
        g_bridge.enqueueAndWait([&]() {
            /* Determine total frame count */
            int totalFrames = 0;
            for (int f = 0; ; f++) {
                JSDebugVarInfo *test = nullptr;
                int fc = JS_DebugGetFrameLocals(g_bridge.rt, f, &test);
                if (fc < 0) break;
                if (test) JS_DebugFreeVarInfo(g_bridge.ctx, test, fc);
                totalFrames++;
            }

            /* Collect from outer frames (frames deeper than frameId) */
            std::vector<std::pair<std::string, JSValue>> closureVars;
            for (int f = totalFrames - 1; f > frameId; f--) {
                JSDebugVarInfo *fvars = nullptr;
                int fvarCount = JS_DebugGetFrameLocals(g_bridge.rt, f, &fvars);
                for (int j = 0; j < fvarCount; j++) {
                    if (fvars[j].name)
                        closureVars.push_back({fvars[j].name, JS_DupValue(g_bridge.ctx, fvars[j].value)});
                }
                if (fvars) JS_DebugFreeVarInfo(g_bridge.ctx, fvars, fvarCount);
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
                    JS_FreeValue(g_bridge.ctx, closureVars[a].second);
                    continue;
                }
                cJSON *v = cJSON_CreateObject();
                cJSON_AddStringToObject(v, "name", closureVars[a].first.c_str());
                formatJSValue(v, closureVars[a].second);
                if (!JS_IsObject(closureVars[a].second)) {
                    cJSON_AddNumberToObject(v, "variablesReference", 0);
                }
                cJSON_AddItemToArray(varsArr, v);
                JS_FreeValue(g_bridge.ctx, closureVars[a].second);
            }
        });
        cJSON *body = cJSON_CreateObject();
        cJSON_AddItemToObject(body, "variables", varsArr);
        g_bridge.sendResponse(requestSeq, "variables", true, nullptr, body);
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
    g_bridge.enqueueAndWait([&]() {
        JSDebugVarInfo *vars = nullptr;
        int varCount = JS_DebugGetFrameLocals(g_bridge.rt, frameId, &vars);

        for (int i = 0; i < varCount; i++) {
            cJSON *v = cJSON_CreateObject();
            cJSON_AddStringToObject(v, "name", vars[i].name ? vars[i].name : "<var>");

            formatJSValue(v, vars[i].value);
            if (!JS_IsObject(vars[i].value)) {
                cJSON_AddNumberToObject(v, "variablesReference", 0);
            }

            cJSON_AddItemToArray(varsArr, v);
        }

        if (vars) JS_DebugFreeVarInfo(g_bridge.ctx, vars, varCount);
    });

    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "variables", varsArr);
    g_bridge.sendResponse(requestSeq, "variables", true, nullptr, body);
}

static void handleEvaluate(cJSON *args, int requestSeq) {
    const char *expr = json_get_str(args, "expression");
    const char *context = json_get_str(args, "context", "repl");
    int frameId = json_get_int(args, "frameId", 0);

    JSValue result = JS_UNDEFINED;
    bool success = false;

    g_bridge.enqueueAndWait([&]() {
        result = JS_DebugEvaluateOnFrameScoped(g_bridge.rt, frameId, expr);
        success = !JS_IsException(result);
    });

    cJSON *body = cJSON_CreateObject();
    if (success) {
        formatJSValue(body, result, "result");
        if (!JS_IsObject(result)) {
            cJSON_AddNumberToObject(body, "variablesReference", 0);
        }
        /* objects already have variablesReference set by formatJSValue */
    } else {
        JSValue exc = JS_GetException(g_bridge.ctx);
        const char *str = JS_ToCString(g_bridge.ctx, exc);
        cJSON_AddStringToObject(body, "result", str ? str : "Error");
        JS_FreeCString(g_bridge.ctx, str);
        JS_FreeValue(g_bridge.ctx, exc);
    }
    JS_FreeValue(g_bridge.ctx, result);

    g_bridge.sendResponse(requestSeq, "evaluate", success, nullptr, body);
}

static void handleThreads(int requestSeq) {
    cJSON *body = cJSON_CreateObject();
    cJSON *threads = cJSON_CreateArray();
    cJSON *t = cJSON_CreateObject();
    cJSON_AddNumberToObject(t, "id", 1);
    cJSON_AddStringToObject(t, "name", "Main Thread");
    cJSON_AddItemToArray(threads, t);
    cJSON_AddItemToObject(body, "threads", threads);
    g_bridge.sendResponse(requestSeq, "threads", true, nullptr, body);
}

static void handleSource(cJSON *args, int requestSeq) {
    cJSON *srcArg = json_get(args, "source");
    const char *path = json_get_str(srcArg, "path");

    if (!path || !path[0]) {
        g_bridge.sendResponse(requestSeq, "source", false, "no source path", nullptr);
        return;
    }

    /* Read the file from disk */
    const char *absPath = toAbsolutePath(path);
    std::ifstream file(absPath);
    if (!file.is_open()) {
        g_bridge.sendResponse(requestSeq, "source", false, "cannot open file", nullptr);
        return;
    }
    std::stringstream ss;
    ss << file.rdbuf();

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "content", ss.str().c_str());
    cJSON_AddStringToObject(body, "mimeType", "text/javascript");
    g_bridge.sendResponse(requestSeq, "source", true, nullptr, body);
}

static void handleLoadedSources(int requestSeq) {
    JSDebugScriptInfo *scripts = nullptr;
    int count = 0;

    g_bridge.enqueueAndWait([&]() {
        count = JS_DebugGetLoadedScripts(g_bridge.rt, &scripts);
    });

    cJSON *sources = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "name", scripts[i].filename);
        cJSON_AddStringToObject(s, "path", toAbsolutePath(scripts[i].filename));
        cJSON_AddNumberToObject(s, "sourceReference", 0);
        cJSON_AddItemToArray(sources, s);
    }

    if (scripts) JS_DebugFreeScriptInfo(g_bridge.rt, scripts, count);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "sources", sources);
    g_bridge.sendResponse(requestSeq, "loadedSources", true, nullptr, body);
}

static void handleDisconnect(int requestSeq) {
    fprintf(stderr, "[DAP] handleDisconnect: enter, running=%d, rt=%p\n",
            g_bridge.running.load(), (void*)g_bridge.rt);
    g_bridge.running = false;
    g_shuttingDown = true;

    if (g_bridge.rt) {
        JS_DebugContinue(g_bridge.rt);
        JS_SetDebugCallback(g_bridge.rt, nullptr, nullptr);
    }

    /* Send response before waiting for thread — client expects immediate ack */
    g_bridge.sendResponse(requestSeq, "disconnect", true, nullptr, nullptr);
    fprintf(stderr, "[DAP] handleDisconnect: response sent\n");

    /* Note: don't join scriptThread here — the main function handles cleanup.
       The reader thread will exit due to g_shuttingDown, and the main function
       will join all threads. */
}

/* ---- Request dispatch ---- */

static void dispatchRequest(cJSON *msg) {
    int requestSeq = json_get_int(msg, "seq");
    const char *command = json_get_str(msg, "command");
    cJSON *args = json_get(msg, "arguments");

    fprintf(stderr, "[DAP] >>> request: seq=%d command=%s\n", requestSeq, command);

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
        g_bridge.configDone = true;
        g_bridge.sendResponse(requestSeq, "configurationDone", true, nullptr, nullptr);
        /* If a stopped event was buffered before configurationDone, send it now */
        if (g_bridge.pendingStop.valid) {
            fprintf(stderr, "[DAP] configurationDone: sending buffered stopped event\n");
            cJSON *body = cJSON_CreateObject();
            cJSON_AddStringToObject(body, "reason", g_bridge.pendingStop.reason.c_str());
            cJSON_AddNumberToObject(body, "threadId", g_bridge.pendingStop.threadId);
            cJSON_AddBoolToObject(body, "allThreadsStopped", true);
            if (g_bridge.pendingStop.bp_id > 0) {
                cJSON *ids = cJSON_CreateArray();
                cJSON_AddItemToArray(ids, cJSON_CreateNumber(g_bridge.pendingStop.bp_id));
                cJSON_AddItemToObject(body, "hitBreakpointIds", ids);
            }
            g_bridge.pushEvent("stopped", body);
            g_bridge.pendingStop.valid = false;
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
        fprintf(stderr, "[DAP] unknown command: %s\n", command);
        g_bridge.sendResponse(requestSeq, command, false, "unknown command", nullptr);
    }
}

/* ---- Main ---- */

int dap_test_bridge_main(int argc, char **argv) {
    init_stdio_binary();

    int port = 0; /* 0 = stdio mode */

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            g_bridge.scriptPath = argv[++i];
        } else if (strcmp(argv[i], "--stop-on-entry") == 0) {
            g_bridge.stopOnEntry = true;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (argv[i][0] != '-') {
            /* Positional argument: treat as script path */
            if (g_bridge.scriptPath.empty()) g_bridge.scriptPath = argv[i];
        }
    }

    /* Normalize scriptPath to absolute with '/' separators */
    if (!g_bridge.scriptPath.empty()) {
        std::string norm = normalizePath(g_bridge.scriptPath.c_str());
        g_bridge.scriptPath = norm;
    }

    /* Initialize transport */
    TcpCtx tcpCtx;
    if (port > 0) {
        if (!init_tcp_transport(&g_bridge.transport, &tcpCtx, port)) {
            fprintf(stderr, "Failed to initialize TCP transport on port %d\n", port);
            return 1;
        }
    } else {
        init_stdio_transport(&g_bridge.transport);
    }

    /* ---- Handler thread: processes DAP requests from the request queue ---- */
    g_bridge.handlerThread = std::thread([]() {
        while (true) {
            std::string message;
            {
                std::unique_lock<std::mutex> lk(g_bridge.reqMutex);
                g_bridge.reqCv.wait(lk, []() {
                    return !g_bridge.reqQueue.empty() || g_shuttingDown.load();
                });
                if (g_shuttingDown.load() && g_bridge.reqQueue.empty()) break;
                message = std::move(g_bridge.reqQueue.front());
                g_bridge.reqQueue.pop();
            }

            cJSON *msg = cJSON_Parse(message.c_str());
            if (!msg) {
                fprintf(stderr, "Failed to parse DAP message\n");
                continue;
            }

            const char *type = json_get_str(msg, "type");
            if (strcmp(type, "request") == 0) {
                dispatchRequest(msg);
            }

            cJSON_Delete(msg);
        }
        fprintf(stderr, "[DAP] handler thread exiting\n");
    });

    /* ---- Main thread: reads DAP messages from transport, pushes to queue ---- */
    std::string message;
    while (dap_read_message(g_bridge.transport, message)) {
        {
            std::lock_guard<std::mutex> lk(g_bridge.reqMutex);
            g_bridge.reqQueue.push(std::move(message));
        }
        g_bridge.reqCv.notify_one();
        message.clear();
    }

    fprintf(stderr, "[DAP] reader loop exited\n");

    /* Signal handler thread to stop */
    g_shuttingDown = true;
    g_bridge.reqCv.notify_one();

    /* If script is still running, unblock it so it can finish */
    if (g_bridge.rt && g_bridge.running.load()) {
        JS_DebugContinue(g_bridge.rt);
        JS_SetDebugCallback(g_bridge.rt, nullptr, nullptr);
    }
    g_bridge.running = false;

    /* Wait for threads */
    if (g_bridge.handlerThread.joinable())
        g_bridge.handlerThread.join();
    if (g_bridge.scriptThread.joinable())
        g_bridge.scriptThread.join();

    if (port > 0) {
        cleanup_tcp(&tcpCtx);
    }

    return 0;
}

#ifndef DAP_TEST_BRIDGE_AS_LIBRARY
int main(int argc, char **argv) {
    return dap_test_bridge_main(argc, argv);
}
#endif
