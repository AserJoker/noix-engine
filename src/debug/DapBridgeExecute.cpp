/*
 * DapBridgeExecute — Script execution logic for DapBridge.
 *
 * Contains the executeScript() method which reads, compiles, and evaluates
 * the JS script with debug info, applies breakpoints, and handles exceptions.
 */

#include "debug/DapBridge.h"
#include "DapBridgeUtils.h"
#include "cJSON.h"

#include <fstream>
#include <sstream>
#include <string>

namespace noix::debug {

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

    /* Pre-warm the source map cache for this script */
    getSourceMap(scriptPath);

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

    /* Fire loadedSource event — report original (TS) path if source map exists */
    {
        int origLine, origCol;
        std::string origPath = resolveOriginalSource(scriptPath, 1, origLine, origCol);

        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "reason", "new");
        cJSON *src = cJSON_CreateObject();
        cJSON_AddStringToObject(src, "name", origPath.c_str());
        cJSON_AddStringToObject(src, "path", origPath.c_str());

        std::ifstream testFile(origPath);
        if (testFile.is_open()) {
            cJSON_AddNumberToObject(src, "sourceReference", 0);
        } else {
            int ref = _nextSourceRef++;
            _sourceRefPaths[ref] = origPath;
            cJSON_AddNumberToObject(src, "sourceReference", ref);
        }

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
