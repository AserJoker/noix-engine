#include "debug/CdpSession.h"
#include <cJSON.h>
#include <cstdio>

namespace noix::debug {

CdpSession::CdpSession(WebSocketServer& server, JsDebugBridge& bridge)
    : _server(server), _bridge(bridge) {
    _dispatcher.registerHandler("Debugger.enable",
        [this](const cJSON* p) { return _bridge.debuggerEnable(p); });
    _dispatcher.registerHandler("Debugger.disable",
        [this](const cJSON*) { return _bridge.debuggerDisable(); });
    _dispatcher.registerHandler("Debugger.pause",
        [this](const cJSON*) { return _bridge.debuggerPause(); });
    _dispatcher.registerHandler("Debugger.resume",
        [this](const cJSON*) { return _bridge.debuggerResume(); });
    _dispatcher.registerHandler("Debugger.stepInto",
        [this](const cJSON*) { return _bridge.debuggerStepInto(); });
    _dispatcher.registerHandler("Debugger.stepOver",
        [this](const cJSON*) { return _bridge.debuggerStepOver(); });
    _dispatcher.registerHandler("Debugger.stepOut",
        [this](const cJSON*) { return _bridge.debuggerStepOut(); });
    _dispatcher.registerHandler("Debugger.setBreakpoint",
        [this](const cJSON* p) { return _bridge.debuggerSetBreakpoint(p); });
    _dispatcher.registerHandler("Debugger.setBreakpointByUrl",
        [this](const cJSON* p) { return _bridge.debuggerSetBreakpointByUrl(p); });
    _dispatcher.registerHandler("Debugger.setBreakpointsActive",
        [this](const cJSON* p) { return _bridge.debuggerSetBreakpointsActive(p); });
    _dispatcher.registerHandler("Debugger.removeBreakpoint",
        [this](const cJSON* p) { return _bridge.debuggerRemoveBreakpoint(p); });
    _dispatcher.registerHandler("Debugger.getPossibleBreakpoints",
        [this](const cJSON* p) { return _bridge.debuggerGetPossibleBreakpoints(p); });
    _dispatcher.registerHandler("Debugger.evaluateOnCallFrame",
        [this](const cJSON* p) { return _bridge.debuggerEvaluateOnCallFrame(p); });
    _dispatcher.registerHandler("Debugger.getScriptSource",
        [this](const cJSON* p) { return _bridge.debuggerGetScriptSource(p); });
    _dispatcher.registerHandler("Runtime.enable",
        [this](const cJSON*) { return _bridge.runtimeEnable(); });
    _dispatcher.registerHandler("Runtime.evaluate",
        [this](const cJSON* p) { return _bridge.runtimeEvaluate(p); });

    // Unimplemented methods — return empty result to avoid errors
    auto emptyResult = [](const cJSON*) -> cJSON* { return cJSON_CreateObject(); };
    _dispatcher.registerHandler("Runtime.getHeapUsage", emptyResult);
    _dispatcher.registerHandler("Runtime.discardConsoleEntries", emptyResult);
    _dispatcher.registerHandler("Network.enable", emptyResult);
    _dispatcher.registerHandler("Network.setAttachDebugStack", emptyResult);
    _dispatcher.registerHandler("Network.setBlockedURLs", emptyResult);
    _dispatcher.registerHandler("Network.emulateNetworkConditionsByRule", emptyResult);
    _dispatcher.registerHandler("Network.overrideNetworkState", emptyResult);
    _dispatcher.registerHandler("Network.clearAcceptedEncodingsOverride", emptyResult);
    _dispatcher.registerHandler("Debugger.setPauseOnExceptions", emptyResult);
    _dispatcher.registerHandler("Debugger.setAsyncCallStackDepth", emptyResult);
    _dispatcher.registerHandler("Debugger.setBlackboxPatterns", emptyResult);
    _dispatcher.registerHandler("Debugger.setSkipAllPauses", emptyResult);
    _dispatcher.registerHandler("Profiler.enable", emptyResult);
    _dispatcher.registerHandler("Profiler.start", emptyResult);
    _dispatcher.registerHandler("Runtime.runIfWaitingForDebugger",
        [this](const cJSON*) -> cJSON* {
            _bridge.notifyDebuggerReady();
            return cJSON_CreateObject();
        });
    _dispatcher.registerHandler("Target.setAutoAttach", emptyResult);
    _dispatcher.registerHandler("Target.getTargetInfo", emptyResult);

    // Runtime methods for Chrome Console compatibility
    _dispatcher.registerHandler("Runtime.callFunctionOn",
        [this](const cJSON* params) -> cJSON* {
            // Chrome Console uses this for expression evaluation
            return _bridge.runtimeCallFunctionOn(params);
        });
    _dispatcher.registerHandler("Runtime.compileScript", emptyResult);
    _dispatcher.registerHandler("Runtime.getProperties",
        [](const cJSON* params) -> cJSON* {
            cJSON* r = cJSON_CreateObject();
            cJSON_AddItemToObject(r, "result", cJSON_CreateArray());
            return r;
        });
    _dispatcher.registerHandler("Runtime.releaseObject", emptyResult);
    _dispatcher.registerHandler("Runtime.releaseObjectGroup", emptyResult);
    _dispatcher.registerHandler("Runtime.globalLexicalScopeNames",
        [](const cJSON*) -> cJSON* {
            cJSON* r = cJSON_CreateObject();
            cJSON_AddItemToObject(r, "names", cJSON_CreateArray());
            return r;
        });
    _dispatcher.registerHandler("Runtime.runScript", emptyResult);
    _dispatcher.registerHandler("Runtime.disable", emptyResult);
    _dispatcher.registerHandler("Runtime.getExceptionDetails", emptyResult);

    // Debugger stubs for Chrome compatibility
    _dispatcher.registerHandler("Debugger.searchInContent",
        [](const cJSON*) -> cJSON* {
            cJSON* r = cJSON_CreateObject();
            cJSON_AddItemToObject(r, "result", cJSON_CreateArray());
            return r;
        });
    _dispatcher.registerHandler("Debugger.setVariableValue", emptyResult);
    _dispatcher.registerHandler("Debugger.restartFrame", emptyResult);
    _dispatcher.registerHandler("Debugger.setInstrumentationBreakpoint", emptyResult);
    _dispatcher.registerHandler("Debugger.setBreakpointOnFunctionCall", emptyResult);
    _dispatcher.registerHandler("Debugger.continueToLocation", emptyResult);
    _dispatcher.registerHandler("Debugger.pauseOnAsyncCall", emptyResult);
    _dispatcher.registerHandler("Debugger.setReturnValue", emptyResult);
    _dispatcher.registerHandler("Debugger.setScriptSource", emptyResult);

    // HeapProfiler stub
    _dispatcher.registerHandler("HeapProfiler.enable", emptyResult);
    _dispatcher.registerHandler("Profiler.disable", emptyResult);
    _dispatcher.registerHandler("Profiler.stop", emptyResult);

    // Additional Network stubs
    _dispatcher.registerHandler("Network.disable", emptyResult);

    // Methods that need proper responses for Chrome DevTools compatibility
    _dispatcher.registerHandler("Runtime.getIsolateId",
        [](const cJSON*) -> cJSON* {
            cJSON* r = cJSON_CreateObject();
            cJSON_AddStringToObject(r, "id", "noix-engine-001");
            return r;
        });

    _dispatcher.registerHandler("Schema.getDomains",
        [](const cJSON*) -> cJSON* {
            cJSON* r = cJSON_CreateObject();
            cJSON* domains = cJSON_CreateArray();
            const char* names[] = {"Runtime", "Debugger", "Profiler", "HeapProfiler", "Schema"};
            const char* versions[] = {"1.3", "1.3", "1.3", "1.3", "1.3"};
            for (int i = 0; i < 5; ++i) {
                cJSON* d = cJSON_CreateObject();
                cJSON_AddStringToObject(d, "name", names[i]);
                cJSON_AddStringToObject(d, "version", versions[i]);
                cJSON_AddItemToArray(domains, d);
            }
            cJSON_AddItemToObject(r, "domains", domains);
            return r;
        });
}

CdpSession::~CdpSession() { stop(); }

void CdpSession::start() {
    _server.setMessageHandler([this](const std::string& msg) { onMessage(msg); });
    _running.store(true);
    _pollThread = std::thread(&CdpSession::eventPollLoop, this);
    _dispatchThread = std::thread(&CdpSession::processMessageLoop, this);
}

void CdpSession::stop() {
    _running.store(false);
    _msgCv.notify_all();
    if (_dispatchThread.joinable()) _dispatchThread.join();
    if (_pollThread.joinable()) _pollThread.join();
}

void CdpSession::onMessage(const std::string& message) {
    // Enqueue message for the dispatch thread — do NOT process here
    // because the WS server thread must not block (it reads more frames).
    {
        std::lock_guard lock(_msgMutex);
        _msgQueue.push(message);
    }
    _msgCv.notify_one();
}

void CdpSession::processMessageLoop() {
    while (_running.load()) {
        std::string message;
        {
            std::unique_lock lock(_msgMutex);
            _msgCv.wait_for(lock, std::chrono::milliseconds(50),
                            [this] { return !_msgQueue.empty() || !_running.load(); });
            if (_msgQueue.empty()) continue;
            message = std::move(_msgQueue.front());
            _msgQueue.pop();
        }

    cJSON* root = cJSON_Parse(message.c_str());
    if (!root) continue;

    cJSON* idItem = cJSON_GetObjectItem(root, "id");
    const char* method = cJSON_GetStringValue(cJSON_GetObjectItem(root, "method"));
    cJSON* params = cJSON_GetObjectItem(root, "params");

    if (method) {
        // Log all incoming CDP messages for debugging
        {
            char* dbg = cJSON_PrintUnformatted(root);
            fprintf(stderr, "CDP FULL < %s\n", dbg);
            cJSON_free(dbg);
        }

        auto handler = _dispatcher.findHandler(method);
        if (handler) {
            cJSON* result = handler(params ? params : cJSON_CreateObject());
            if (idItem) {
                int id = (int)idItem->valuedouble;
                sendResponse(id, result);
                // Flush events immediately — Chrome expects events like
                // executionContextCreated, scriptParsed, breakpointResolved
                // to arrive right after the response, not 10ms later.
                flushPendingEvents();
            } else {
                cJSON_Delete(result);
            }
        } else {
            if (idItem) {
                int id = (int)idItem->valuedouble;
                fprintf(stderr, "CDP METHOD NOT FOUND: %s\n", method);
                sendError(id, -32601, std::string("Method not found: ") + method);
            }
        }
    }

    cJSON_Delete(root);
    } // end while loop
}

void CdpSession::sendResponse(int id, cJSON* result) {
    cJSON* resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "id", id);
    if (result) {
        cJSON_AddItemToObject(resp, "result", result);
    } else {
        cJSON_AddItemToObject(resp, "result", cJSON_CreateObject());
    }

    char* str = cJSON_PrintUnformatted(resp);
    _server.send(str);
    fprintf(stderr, "CDP > %s\n", str);
    cJSON_free(str);
    cJSON_Delete(resp);
}

void CdpSession::sendError(int id, int code, const std::string& message) {
    cJSON* resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "id", id);
    cJSON* err = cJSON_CreateObject();
    cJSON_AddNumberToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", message.c_str());
    cJSON_AddItemToObject(resp, "error", err);

    char* str = cJSON_PrintUnformatted(resp);
    _server.send(str);
    fprintf(stderr, "CDP ERROR > %s\n", str);
    cJSON_free(str);
    cJSON_Delete(resp);
}

void CdpSession::sendEvent(const std::string& method, cJSON* params) {
    cJSON* evt = cJSON_CreateObject();
    cJSON_AddStringToObject(evt, "method", method.c_str());
    if (params) {
        cJSON_AddItemToObject(evt, "params", params);
    } else {
        cJSON_AddItemToObject(evt, "params", cJSON_CreateObject());
    }

    char* str = cJSON_PrintUnformatted(evt);
    _server.send(str);
    fprintf(stderr, "CDP EVENT > %s\n", str);
    cJSON_free(str);
    cJSON_Delete(evt);
}

void CdpSession::flushPendingEvents() {
    auto events = _bridge.pollEvents();
    for (auto& [method, params] : events) {
        sendEvent(method, params);
    }
}

void CdpSession::eventPollLoop() {
    int loopCount = 0;
    while (_running.load()) {
        auto events = _bridge.pollEvents();
        for (auto& [method, params] : events) {
            sendEvent(method, params);
        }
        if (++loopCount % 100 == 0) {
            fprintf(stderr, "eventPollLoop: still running (loop %d)\n", loopCount);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    fprintf(stderr, "eventPollLoop: exiting\n");
}

} // namespace noix::debug
