/*
 * DapBridgeHandlers — DAP request handlers and dispatch.
 *
 * Contains all handle* methods and the dispatchRequest routing logic.
 */

#include "debug/DapBridge.h"
#include "DapBridgeUtils.h"
#include "script/ScriptEngine.h"
#include "cJSON.h"
#include "core/Logger.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

namespace noix::debug {

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

    /* Resume the script thread if it's paused in a debug callback.
       Only call JS_DebugContinue if the debugger is actually paused —
       otherwise the enqueueAndWait would block waiting for the script
       thread to process the task, adding unnecessary latency. */
    if (rt && JS_DebugGetState(rt) != 0) {
        /* Debugger is paused — must resume via enqueueAndWait so the
           command runs on the script thread through drainQueue. */
        enqueueAndWait([this]() {
            JS_DebugContinue(rt);
        });
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
    struct BpReq { int line; std::string condition; int originalLine; };
    std::vector<BpReq> requestedBps;
    if (bps && cJSON_IsArray(bps)) {
        int arrSize = cJSON_GetArraySize(bps);
        for (int bi = 0; bi < arrSize; bi++) {
            cJSON *bp = cJSON_GetArrayItem(bps, bi);
            BpReq req;
            req.line = json_get_int(bp, "line");
            req.condition = json_get_str(bp, "condition", "");
            req.originalLine = req.line; /* save original line for response */
            requestedBps.push_back(req);
        }
    }

    /* Results array -- filled during script-thread execution or for pending */
    cJSON *result = cJSON_CreateArray();

    /* Check if the client path is a TypeScript source and needs translation */
    std::string clientPath(path ? path : "");
    std::string resolvedPath = normalizePath(clientPath.c_str());
    bool isTsSource = clientPath.size() > 3 && clientPath.substr(clientPath.size() - 3) == ".ts";

    /* If it's a .ts file, translate to .js path and line numbers */
    if (isTsSource) {
        /* Pre-warm source map cache: resolve .ts → .js path and load the source map.
           Without this, resolveGeneratedSource finds an empty cache and falls back
           to simple extension replacement with wrong line numbers. */
        std::string jsFallback = clientPath.substr(0, clientPath.size() - 3) + ".js";
        std::string jsAbsPath = normalizePath(jsFallback.c_str());
        getSourceMap(jsAbsPath);

        int genLine, genCol;
        std::string jsPath = resolveGeneratedSource(clientPath, 0, genLine, genCol);
        resolvedPath = normalizePath(jsPath.c_str());
        /* Translate each breakpoint's line from TS to JS */
        for (auto &req : requestedBps) {
            int gl = 0, gc = 0;
            jsPath = resolveGeneratedSource(clientPath, req.line, gl, gc);
            req.line = (gl > 0) ? gl : req.line;
        }
    }

    /* Normalize the resolved path for comparison with QuickJS scripts */
    std::string normPath = resolvedPath;

    /* Helper: check if a breakpoint filename matches the resolved path */
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
                    cJSON_AddNumberToObject(r, "line", req.originalLine);
                    cJSON_AddItemToArray(result, r);
                } else {
                    cJSON *r = cJSON_CreateObject();
                    cJSON_AddBoolToObject(r, "verified", false);
                    cJSON_AddNumberToObject(r, "line", req.originalLine);
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
                cJSON_AddNumberToObject(r, "line", req.originalLine);
                cJSON_AddItemToArray(result, r);
            } else {
                cJSON *r = cJSON_CreateObject();
                cJSON_AddBoolToObject(r, "verified", false);
                cJSON_AddNumberToObject(r, "line", req.originalLine);
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
            pb.filename = resolvedPath; /* store JS path for QuickJS */
            pb.line = req.line;         /* JS line number for QuickJS */
            pb.condition = req.condition;
            pendingBreakpoints.push_back(pb);

            cJSON *r = cJSON_CreateObject();
            cJSON_AddBoolToObject(r, "verified", true);
            cJSON_AddNumberToObject(r, "line", req.originalLine);
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

        /* Try to translate JS path/line to original (TS) via source map */
        int origLine, origCol;
        std::string displayPath = resolveOriginalSource(fname, frames[i].line, origLine, origCol);

        cJSON_AddStringToObject(src, "name", displayPath.c_str());
        cJSON_AddStringToObject(src, "path", displayPath.c_str());

        /* If the original source is not on disk, assign a sourceReference */
        std::ifstream testFile(displayPath);
        if (testFile.is_open()) {
            cJSON_AddNumberToObject(src, "sourceReference", 0);
        } else {
            int ref = _nextSourceRef++;
            _sourceRefPaths[ref] = displayPath;
            cJSON_AddNumberToObject(src, "sourceReference", ref);
        }

        cJSON_AddItemToObject(f, "source", src);
        cJSON_AddNumberToObject(f, "line", origLine);
        cJSON_AddNumberToObject(f, "column", origCol);
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
        JSValue obj = _objectRefs.find(varRef);
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
                    formatJSValue(ctx, _objectRefs, v, propVal);
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
                formatJSValue(ctx, _objectRefs, v, propVal);
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
                formatJSValue(ctx, _objectRefs, v, closureVars[a].second);
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

            formatJSValue(ctx, _objectRefs, v, vars[i].value);
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
    std::string errorStr;
    std::string resultStr;
    std::string resultType;
    int varRef = 0;

    /* All QuickJS API calls must run on the script thread via enqueueAndWait.
       Capture the formatted result as strings so no JSValue escapes the block. */
    enqueueAndWait([&]() {
        result = JS_DebugEvaluateOnFrameScoped(rt, frameId, expr);
        success = !JS_IsException(result);

        if (success) {
            if (JS_IsNumber(result)) {
                double d;
                JS_ToFloat64(ctx, &d, result);
                char buf[64];
                snprintf(buf, sizeof(buf), "%g", d);
                resultStr = buf;
                resultType = "number";
            } else if (JS_IsBool(result)) {
                resultStr = JS_ToBool(ctx, result) ? "true" : "false";
                resultType = "boolean";
            } else if (JS_IsString(result)) {
                const char *s = JS_ToCString(ctx, result);
                resultStr = s ? s : "";
                JS_FreeCString(ctx, s);
                resultType = "string";
            } else if (JS_IsNull(result)) {
                resultStr = "null";
                resultType = "null";
            } else if (JS_IsUndefined(result)) {
                resultStr = "undefined";
                resultType = "undefined";
            } else if (JS_IsObject(result)) {
                if (JS_IsArray(result)) {
                    JSAtom lengthAtom = JS_NewAtom(ctx, "length");
                    JSValue lenVal = JS_GetProperty(ctx, result, lengthAtom);
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
                    resultStr = buf;
                    resultType = "object";
                } else if (JS_IsFunction(ctx, result)) {
                    resultStr = "function";
                    resultType = "function";
                } else {
                    resultStr = "Object";
                    resultType = "object";
                }
                varRef = _objectRefs.add(JS_DupValue(ctx, result));
            }
        } else {
            JSValue exc = JS_GetException(ctx);
            const char *str = JS_ToCString(ctx, exc);
            errorStr = str ? str : "Error";
            JS_FreeCString(ctx, str);
            JS_FreeValue(ctx, exc);
        }
        JS_FreeValue(ctx, result);
    });

    cJSON *body = cJSON_CreateObject();
    if (success) {
        cJSON_AddStringToObject(body, "result", resultStr.c_str());
        if (!resultType.empty()) {
            cJSON_AddStringToObject(body, "type", resultType.c_str());
        }
        cJSON_AddNumberToObject(body, "variablesReference", varRef);
    } else {
        cJSON_AddStringToObject(body, "result", errorStr.c_str());
    }

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
    int sourceRef = json_get_int(args, "sourceReference", 0);

    /* If sourceReference > 0, look up the path from our cache */
    if (sourceRef > 0) {
        auto it = _sourceRefPaths.find(sourceRef);
        if (it == _sourceRefPaths.end()) {
            sendResponse(requestSeq, "source", false, "unknown source reference", nullptr);
            return;
        }
        std::ifstream file(it->second);
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
        return;
    }

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
        const char *fname = scripts[i].filename;
        std::string absPath = toAbsolutePath(fname);

        /* Report the original (TS) source if a source map exists */
        int origLine, origCol;
        std::string origPath = resolveOriginalSource(fname, 1, origLine, origCol);

        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "name", origPath.c_str());
        cJSON_AddStringToObject(s, "path", origPath.c_str());

        /* If original source is on disk, sourceReference = 0; otherwise assign one */
        std::ifstream testFile(origPath);
        if (testFile.is_open()) {
            cJSON_AddNumberToObject(s, "sourceReference", 0);
        } else {
            int ref = _nextSourceRef++;
            _sourceRefPaths[ref] = origPath;
            cJSON_AddNumberToObject(s, "sourceReference", ref);
        }

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

} // namespace noix::debug
