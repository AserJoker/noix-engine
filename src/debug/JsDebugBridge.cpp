#include "debug/JsDebugBridge.h"
#include "core/Logger.h"
#include <SDL3/SDL.h>
#include <cJSON.h>
#include <quickjs.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

// Count lines and compute last-line end column for scriptParsed event
void computeLineInfo(const std::string& code, int& endLine, int& endColumn) {
    endLine = 0;
    endColumn = 0;
    int lastLineStart = 0;
    for (size_t i = 0; i < code.size(); ++i) {
        if (code[i] == '\n') {
            endLine++;
            endColumn = static_cast<int>(i - lastLineStart);
            lastLineStart = static_cast<int>(i) + 1;
        }
    }
    // Handle last line (no trailing newline)
    if (!code.empty()) {
        if (code.back() == '\n') {
            endColumn = 0;
        } else {
            endColumn = static_cast<int>(code.size() - lastLineStart);
        }
    }
}

// Simple hash for script (djb2)
std::string computeScriptHash(const std::string& code) {
    unsigned long hash = 5381;
    for (char c : code) {
        hash = ((hash << 5) + hash) + static_cast<unsigned char>(c);
    }
    char buf[17];
    snprintf(buf, sizeof(buf), "%016lx", hash);
    return std::string(buf);
}

// Simple regex matcher for CDP urlRegex patterns.
// Chrome sends patterns like "simple\\.js$" or "file:///D:/path/script\\.js$"
// We don't need full regex — just handle common patterns:
//   - escaped dots: \. → match literal .
//   - $: match end of string
//   - .*: match any characters
//   - parentheses/alternation: flatten
bool urlRegexMatchesUrl(const std::string& regex, const std::string& url) {
    // Strategy: extract literal segments from the regex and check they all
    // appear in the URL in order. Handle $ (end anchor).
    bool anchorEnd = (!regex.empty() && regex.back() == '$');
    std::string pattern = regex;
    if (anchorEnd) pattern.pop_back();

    // Remove leading ^ if present
    if (!pattern.empty() && pattern.front() == '^') pattern.erase(pattern.begin());

    // Extract literal segments (between .* and other regex constructs)
    std::vector<std::string> literals;
    std::string current;
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '\\' && i + 1 < pattern.size()) {
            current += pattern[i + 1];  // Escaped char (e.g. \. → .)
            ++i;
        } else if (pattern[i] == '.' && i + 1 < pattern.size() && pattern[i + 1] == '*') {
            // .* — wildcard, flush current literal
            if (!current.empty()) {
                literals.push_back(current);
                current.clear();
            }
            ++i;  // skip *
        } else if (pattern[i] == '(' || pattern[i] == ')' ||
                   pattern[i] == '|' || pattern[i] == '[' || pattern[i] == ']') {
            // Regex constructs — flush and skip
            if (!current.empty()) {
                literals.push_back(current);
                current.clear();
            }
        } else {
            current += pattern[i];
        }
    }
    if (!current.empty()) literals.push_back(current);

    if (literals.empty()) return true;  // No constraints

    // Check all literals appear in the URL in order
    size_t pos = 0;
    for (const auto& lit : literals) {
        size_t found = url.find(lit, pos);
        if (found == std::string::npos) return false;
        pos = found + lit.size();
    }

    // If anchored to end, the last literal must reach the URL end
    if (anchorEnd && !literals.empty()) {
        const auto& last = literals.back();
        if (url.size() < last.size()) return false;
        if (url.compare(url.size() - last.size(), last.size(), last) != 0) return false;
    }

    return true;
}

} // anonymous namespace

namespace noix::debug {

JsDebugBridge::JsDebugBridge() = default;

JsDebugBridge::~JsDebugBridge() {
    stop();
}

bool JsDebugBridge::start(const std::string& scriptPath, bool debugWait) {
    if (_running.load()) return true;
    _debugWait = debugWait;

    // Convert to absolute path and normalize separators for URL generation
    try {
        auto absPath = std::filesystem::absolute(scriptPath);
        _scriptPath = absPath.generic_string();  // Uses forward slashes
    } catch (...) {
        _scriptPath = scriptPath;
    }

    _running.store(true);
    _scriptThread = std::thread(&JsDebugBridge::scriptThreadFunc, this);
    return true;
}

void JsDebugBridge::stop() {
    if (!_running.load()) return;
    _running.store(false);
    if (_scriptThread.joinable()) _scriptThread.join();
}

void JsDebugBridge::setEventHandler(EventHandler handler) {
    // Not needed — we use pollEvents
}

std::vector<std::pair<std::string, cJSON*>> JsDebugBridge::pollEvents() {
    std::lock_guard lock(_evtMutex);
    std::vector<std::pair<std::string, cJSON*>> events;
    for (auto& e : _evtQueue) {
        events.emplace_back(e.method, e.params);
    }
    if (!_evtQueue.empty()) {
        core::Logger::instance().info("pollEvents: returning {} events", _evtQueue.size());
    }
    _evtQueue.clear();
    return events;
}

// ---- Script thread ----

void JsDebugBridge::scriptThreadFunc() {
    _rt = JS_NewRuntime();
    _ctx = JS_NewContext(_rt);

    JS_SetDebugCallback(_rt, debugCallback, this);
    JS_SetDebugDrainQueue(_rt, drainQueue);

    // Read script file
    std::ifstream file(_scriptPath);
    if (!file.is_open()) {
        core::Logger::instance().error("Cannot open script: {}", _scriptPath);
        JS_FreeContext(_ctx);
        JS_FreeRuntime(_rt);
        _ctx = nullptr;
        _rt = nullptr;
        _running.store(false);
        return;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string code = ss.str();

    // Register script
    std::string scriptId = std::to_string(_nextScriptId++);
    _filenameToId[_scriptPath] = scriptId;
    _idToFilename[scriptId] = _scriptPath;
    _scriptIdToSource[scriptId] = code;

    // Register URL mapping (file:///D:/... -> D:/...)
    std::string fileUrl = "file:///" + _scriptPath;
    _urlToFilename[fileUrl] = _scriptPath;
    _urlToFilename[_scriptPath] = _scriptPath;

    // If debugWait mode, wait for Debugger.enable before evaluating
    if (_debugWait) {
        while (_running.load() && !_debuggerEnabled) {
            processCommands();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        core::Logger::instance().info("Debugger attached, waiting for runIfWaitingForDebugger...");
        while (_running.load() && !_debuggerReady.load()) {
            processCommands();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        processCommands();
        core::Logger::instance().info("Debugger ready, evaluating script...");
    }

    // Fire Debugger.scriptParsed event (skip if already sent by debuggerEnable
    // during debug-wait, which fires scriptParsed for all registered scripts)
    if (_debuggerEnabled && !_debugWait) {
        std::string url = "file:///" + _scriptPath;

        int endLine = 0, endColumn = 0;
        computeLineInfo(code, endLine, endColumn);
        std::string hash = computeScriptHash(code);

        cJSON* params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "scriptId", scriptId.c_str());
        cJSON_AddStringToObject(params, "url", url.c_str());
        cJSON_AddNumberToObject(params, "startLine", 0);
        cJSON_AddNumberToObject(params, "startColumn", 0);
        cJSON_AddNumberToObject(params, "endLine", endLine);
        cJSON_AddNumberToObject(params, "endColumn", endColumn);
        cJSON_AddNumberToObject(params, "executionContextId", 1);
        cJSON_AddStringToObject(params, "hash", hash.c_str());
        cJSON_AddBoolToObject(params, "isModule", false);
        cJSON_AddNumberToObject(params, "length", static_cast<double>(code.size()));
        cJSON_AddStringToObject(params, "scriptLanguage", "JavaScript");
        pushEvent("Debugger.scriptParsed", params);
    }

    // Evaluate script
    if (_debugWait) {
        core::Logger::instance().info("debug-wait mode: calling JS_DebugPause before eval");
        JS_DebugPause(_rt);
    }

    core::Logger::instance().info("Starting JS_Eval...");
    JSValue ret = JS_Eval(_ctx, code.c_str(), code.size(), _scriptPath.c_str(),
                           JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_DEBUG_INFO);
    core::Logger::instance().info("JS_Eval returned");

    if (JS_IsException(ret)) {
        JSValue e = JS_GetException(_ctx);
        const char* s = JS_ToCString(_ctx, e);
        core::Logger::instance().error("Script eval error: {}", s ? s : "(null)");
        JS_FreeCString(_ctx, s);
        JS_FreeValue(_ctx, e);
    } else {
        core::Logger::instance().info("Script eval completed successfully");
    }
    JS_FreeValue(_ctx, ret);

    // Keep running to process commands
    while (_running.load()) {
        processCommands();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    JS_FreeContext(_ctx);
    JS_FreeRuntime(_rt);
    _ctx = nullptr;
    _rt = nullptr;
}

void JsDebugBridge::debugCallback(JSRuntime* rt, JSDebugEventType event,
                                    const char* filename,
                                    int line, int col, uint32_t bp_id, void* opaque) {
    auto* self = static_cast<JsDebugBridge*>(opaque);

    core::Logger::instance().info("debugCallback ENTER: event={} line={} col={} skipLine={}",
                                   (int)event, line, col, self->_skipLine);

    // Skip re-hitting the same location after resume/step.
    // Must preserve stepping mode — JS_DebugContinue would cancel it.
    if (self->_skipLine >= 0 && filename &&
        self->_skipFilename == filename && self->_skipLine == line) {
        core::Logger::instance().info("debugCallback: skipping same location line={}", line);
        if (self->_lastStepKind >= 0) {
            JS_DebugStep(rt, self->_lastStepKind);
        } else {
            JS_DebugContinue(rt);
        }
        return;
    }
    self->_skipLine = -1;  // clear skip after first different location

    const char* reason = "";
    if (event == JS_DEBUG_EVENT_BREAKPOINT_HIT) reason = "breakpoint";
    else if (event == JS_DEBUG_EVENT_STEP_COMPLETE) reason = "step";
    else if (event == JS_DEBUG_EVENT_DEBUGGER_STMT) reason = "debugger statement";

    core::Logger::instance().info("debugCallback: event={} line={} col={} reason={}",
                                   (int)event, line, col, reason);

    // Build callFrames
    cJSON* callFrames = self->buildCallFrames();

    cJSON* params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "callFrames", callFrames);
    cJSON_AddStringToObject(params, "reason", reason);

    self->pushEvent("Debugger.paused", params);
    core::Logger::instance().info("debugCallback: pushed Debugger.paused event");

    // Remember current pause location for skip-after-resume
    self->_skipFilename = filename ? filename : "";
    self->_skipLine = line;
}

void JsDebugBridge::drainQueue(void* opaque) {
    auto* self = static_cast<JsDebugBridge*>(opaque);
    core::Logger::instance().info("drainQueue called");
    self->processCommands();
}

void JsDebugBridge::processCommands() {
    std::queue<Command*> cmds;
    {
        std::lock_guard lock(_cmdMutex);
        cmds.swap(_cmdQueue);
    }

    while (!cmds.empty()) {
        Command* cmd = cmds.front();
        cmds.pop();

        cmd->result = cmd->execute();
        cmd->signalDone();
    }
}

void JsDebugBridge::pushEvent(const std::string& method, cJSON* params) {
    std::lock_guard lock(_evtMutex);
    _evtQueue.push_back({method, params});
}

cJSON* JsDebugBridge::enqueueAndWait(std::function<cJSON*()> fn) {
    Command cmd;
    cmd.execute = std::move(fn);
    cmd.result = nullptr;

    {
        std::lock_guard lock(_cmdMutex);
        _cmdQueue.push(&cmd);
    }

    // Wait up to 3 seconds for the script thread to process
    if (cmd.waitFor(std::chrono::milliseconds(3000))) {
        return cmd.result ? cmd.result : cJSON_CreateObject();
    }
    // Timeout — return empty result
    core::Logger::instance().warn("Command timed out in enqueueAndWait");
    return cJSON_CreateObject();
}

// ---- CDP command handlers (WS thread) ----
// Handlers that only touch shared data (no QuickJS API calls) run directly.
// Handlers that call QuickJS APIs go through enqueueAndWait().

cJSON* JsDebugBridge::debuggerEnable(const cJSON* /*params*/) {
    _debuggerEnabled = true;

    // If scripts are already registered, send scriptParsed events now
    for (auto& [filename, scriptId] : _filenameToId) {
        std::string url = "file:///" + filename;

        auto srcIt = _scriptIdToSource.find(scriptId);
        std::string source = (srcIt != _scriptIdToSource.end()) ? srcIt->second : "";
        int endLine = 0, endColumn = 0;
        computeLineInfo(source, endLine, endColumn);
        std::string hash = computeScriptHash(source);

        cJSON* params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "scriptId", scriptId.c_str());
        cJSON_AddStringToObject(params, "url", url.c_str());
        cJSON_AddNumberToObject(params, "startLine", 0);
        cJSON_AddNumberToObject(params, "startColumn", 0);
        cJSON_AddNumberToObject(params, "endLine", endLine);
        cJSON_AddNumberToObject(params, "endColumn", endColumn);
        cJSON_AddNumberToObject(params, "executionContextId", 1);
        cJSON_AddStringToObject(params, "hash", hash.c_str());
        cJSON_AddBoolToObject(params, "isModule", false);
        cJSON_AddNumberToObject(params, "length", static_cast<double>(source.size()));
        cJSON_AddStringToObject(params, "scriptLanguage", "JavaScript");
        pushEvent("Debugger.scriptParsed", params);
    }

    return cJSON_CreateObject();
}

cJSON* JsDebugBridge::debuggerDisable() {
    _debuggerEnabled = false;
    return cJSON_CreateObject();
}

cJSON* JsDebugBridge::debuggerPause() {
    if (_rt) JS_DebugPause(_rt);
    return cJSON_CreateObject();
}

cJSON* JsDebugBridge::debuggerResume() {
    _lastStepKind = -1;
    if (_rt) JS_DebugContinue(_rt);
    pushEvent("Debugger.resumed", cJSON_CreateObject());
    return cJSON_CreateObject();
}

cJSON* JsDebugBridge::debuggerStepInto() {
    _lastStepKind = 0;
    if (_rt) JS_DebugStep(_rt, 0);
    pushEvent("Debugger.resumed", cJSON_CreateObject());
    return cJSON_CreateObject();
}

cJSON* JsDebugBridge::debuggerStepOver() {
    _lastStepKind = 1;
    if (_rt) JS_DebugStep(_rt, 1);
    pushEvent("Debugger.resumed", cJSON_CreateObject());
    return cJSON_CreateObject();
}

cJSON* JsDebugBridge::debuggerStepOut() {
    _lastStepKind = 2;
    if (_rt) JS_DebugStep(_rt, 2);
    pushEvent("Debugger.resumed", cJSON_CreateObject());
    return cJSON_CreateObject();
}

cJSON* JsDebugBridge::debuggerSetBreakpointByUrl(const cJSON* params) {
    const cJSON* lineItem = cJSON_GetObjectItem(params, "lineNumber");
    const cJSON* urlItem = cJSON_GetObjectItem(params, "url");
    const cJSON* urlRegexItem = cJSON_GetObjectItem(params, "urlRegex");
    const cJSON* scriptIdItem = cJSON_GetObjectItem(params, "scriptId");

    if (!lineItem) return cJSON_CreateObject();

    int cdpLine = (int)lineItem->valuedouble;
    int qjsLine = cdpLine + 1;

    // Resolve filename from url, urlRegex, or scriptId
    std::string filename;
    if (urlItem && urlItem->valuestring) {
        std::string url = urlItem->valuestring;
        auto it = _urlToFilename.find(url);
        if (it != _urlToFilename.end()) {
            filename = it->second;
        } else {
            for (auto& [u, f] : _urlToFilename) {
                if (u.size() >= url.size() &&
                    u.compare(u.size() - url.size(), url.size(), url) == 0) {
                    filename = f;
                    break;
                }
            }
            if (filename.empty()) {
                if (url.substr(0, 8) == "file:///") {
                    filename = url.substr(8);
                    if (filename.size() > 2 && filename[0] == '/' && filename[2] == ':') {
                        filename = filename.substr(1);
                    }
                } else {
                    filename = url;
                }
            }
        }
    } else if (urlRegexItem && urlRegexItem->valuestring) {
        // Chrome DevTools uses urlRegex when clicking line numbers in Sources panel.
        // Match against known script URLs using simple substring matching.
        std::string regex = urlRegexItem->valuestring;
        for (auto& [url, fn] : _urlToFilename) {
            // Try to match the regex against known URLs.
            // Common patterns: "filename\\.js$", "path/filename\\.js"
            // Simple approach: extract the literal parts from the regex and match.
            if (urlRegexMatchesUrl(regex, url)) {
                filename = fn;
                break;
            }
        }
    } else if (scriptIdItem && scriptIdItem->valuestring) {
        auto it = _idToFilename.find(scriptIdItem->valuestring);
        if (it != _idToFilename.end()) filename = it->second;
    }

    if (filename.empty()) {
        cJSON* result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "breakpointId", "");
        return result;
    }

    // V8-style breakpoint ID: <scriptId>:<lineNumber>:<columnNumber>:<url>
    // Chrome uses this format to parse the breakpoint location from the ID.
    auto it2 = _filenameToId.find(filename);
    std::string resolvedScriptId = (it2 != _filenameToId.end()) ? it2->second : "0";

    std::string bpUrl;
    auto urlIt = std::find_if(_urlToFilename.begin(), _urlToFilename.end(),
        [&](const auto& pair) { return pair.second == filename; });
    if (urlIt != _urlToFilename.end()) bpUrl = urlIt->first;

    std::string cdpBpId = resolvedScriptId + ":" + std::to_string(cdpLine) + ":0:" + bpUrl;

    // JS_DebugSetBreakpoint only modifies the breakpoint array, safe to call
    // from WS thread (it sets atomic flags, doesn't touch eval loop state).
    uint32_t qjsBpId = 0;
    if (_rt) {
        qjsBpId = JS_DebugSetBreakpoint(_rt, filename.c_str(), qjsLine);
    }

    if (qjsBpId > 0) {
        _cdpToQjs[cdpBpId] = qjsBpId;
        _qjsToCdp[qjsBpId] = cdpBpId;
    }

    cJSON* result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "breakpointId", cdpBpId.c_str());

    cJSON* locations = cJSON_CreateArray();
    cJSON* loc = cJSON_CreateObject();
    cJSON_AddStringToObject(loc, "scriptId", resolvedScriptId.c_str());
    cJSON_AddNumberToObject(loc, "lineNumber", cdpLine);
    cJSON_AddNumberToObject(loc, "columnNumber", 0);
    cJSON_AddItemToArray(locations, loc);
    cJSON_AddItemToObject(result, "locations", locations);

    // Send breakpointResolved event — Chrome DevTools needs this to
    // confirm the breakpoint is bound to a script location in the Sources panel
    if (!resolvedScriptId.empty()) {
        cJSON* evtParams = cJSON_CreateObject();
        cJSON_AddStringToObject(evtParams, "breakpointId", cdpBpId.c_str());
        cJSON* resolvedLoc = cJSON_CreateObject();
        cJSON_AddStringToObject(resolvedLoc, "scriptId", resolvedScriptId.c_str());
        cJSON_AddNumberToObject(resolvedLoc, "lineNumber", cdpLine);
        cJSON_AddNumberToObject(resolvedLoc, "columnNumber", 0);
        cJSON_AddItemToObject(evtParams, "location", resolvedLoc);
        pushEvent("Debugger.breakpointResolved", evtParams);
    }

    return result;
}

cJSON* JsDebugBridge::debuggerSetBreakpointsActive(const cJSON* /*params*/) {
    return cJSON_CreateObject();
}

cJSON* JsDebugBridge::debuggerSetBreakpoint(const cJSON* params) {
    const cJSON* loc = cJSON_GetObjectItem(params, "location");
    if (!loc) return cJSON_CreateObject();

    const cJSON* scriptIdItem = cJSON_GetObjectItem(loc, "scriptId");
    const cJSON* lineItem = cJSON_GetObjectItem(loc, "lineNumber");

    if (!scriptIdItem || !lineItem) return cJSON_CreateObject();

    std::string scriptId = scriptIdItem->valuestring;
    int cdpLine = (int)lineItem->valuedouble;
    int qjsLine = cdpLine + 1;

    auto it = _idToFilename.find(scriptId);
    if (it == _idToFilename.end()) return cJSON_CreateObject();

    const char* filename = it->second.c_str();
    uint32_t qjsBpId = _rt ? JS_DebugSetBreakpoint(_rt, filename, qjsLine) : 0;
    if (qjsBpId == 0) return cJSON_CreateObject();

    // V8-style breakpoint ID for setBreakpoint (by location)
    std::string cdpBpId = scriptId + ":" + std::to_string(cdpLine) + ":0";
    _cdpToQjs[cdpBpId] = qjsBpId;
    _qjsToCdp[qjsBpId] = cdpBpId;

    cJSON* result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "breakpointId", cdpBpId.c_str());

    cJSON* actualLoc = cJSON_CreateObject();
    cJSON_AddStringToObject(actualLoc, "scriptId", scriptId.c_str());
    cJSON_AddNumberToObject(actualLoc, "lineNumber", cdpLine);
    cJSON_AddItemToObject(result, "actualLocation", actualLoc);

    return result;
}

cJSON* JsDebugBridge::debuggerRemoveBreakpoint(const cJSON* params) {
    const cJSON* idItem = cJSON_GetObjectItem(params, "breakpointId");
    if (!idItem) return cJSON_CreateObject();

    std::string cdpBpId = idItem->valuestring;
    auto it = _cdpToQjs.find(cdpBpId);
    if (it == _cdpToQjs.end()) return cJSON_CreateObject();

    uint32_t qjsBpId = it->second;
    if (_rt) JS_DebugRemoveBreakpoint(_rt, qjsBpId);
    _qjsToCdp.erase(qjsBpId);
    _cdpToQjs.erase(cdpBpId);

    return cJSON_CreateObject();
}

cJSON* JsDebugBridge::debuggerGetPossibleBreakpoints(const cJSON* params) {
    // Return the requested start location as a possible breakpoint.
    // Chrome uses this to confirm that a line can have a breakpoint.
    const cJSON* start = cJSON_GetObjectItem(params, "start");
    cJSON* result = cJSON_CreateObject();
    cJSON* locations = cJSON_CreateArray();

    if (start) {
        const cJSON* scriptIdItem = cJSON_GetObjectItem(start, "scriptId");
        const cJSON* lineItem = cJSON_GetObjectItem(start, "lineNumber");
        if (scriptIdItem && lineItem) {
            cJSON* loc = cJSON_CreateObject();
            cJSON_AddStringToObject(loc, "scriptId", scriptIdItem->valuestring);
            cJSON_AddNumberToObject(loc, "lineNumber", (int)lineItem->valuedouble);
            cJSON_AddNumberToObject(loc, "columnNumber", 0);
            cJSON_AddItemToArray(locations, loc);
        }
    }

    cJSON_AddItemToObject(result, "locations", locations);
    return result;
}

cJSON* JsDebugBridge::debuggerEvaluateOnCallFrame(const cJSON* params) {
    // Must run on script thread — calls JS_DebugEvaluateOnFrame
    const cJSON* frameIdItem = cJSON_GetObjectItem(params, "callFrameId");
    const cJSON* exprItem = cJSON_GetObjectItem(params, "expression");
    if (!exprItem) return cJSON_CreateObject();

    int frameIndex = 0;
    if (frameIdItem && frameIdItem->valuestring) {
        std::string fid = frameIdItem->valuestring;
        if (fid.substr(0, 5) == "frame") {
            frameIndex = std::stoi(fid.substr(5));
        }
    }

    std::string expr = exprItem->valuestring;

    return enqueueAndWait([this, frameIndex, expr]() -> cJSON* {
        JSValue val = JS_DebugEvaluateOnFrame(_rt, frameIndex, expr.c_str());

        cJSON* result = cJSON_CreateObject();
        cJSON* ro = jsValueToRemoteObject(_ctx, val);
        cJSON_AddItemToObject(result, "result", ro);

        if (JS_IsException(val)) {
            JSValue e = JS_GetException(_ctx);
            cJSON* exc = jsValueToRemoteObject(_ctx, e);
            cJSON_AddItemToObject(result, "exceptionDetails", exc);
            JS_FreeValue(_ctx, e);
        }

        JS_FreeValue(_ctx, val);
        return result;
    });
}

cJSON* JsDebugBridge::debuggerGetScriptSource(const cJSON* params) {
    // Pure data lookup — safe on WS thread
    const cJSON* idItem = cJSON_GetObjectItem(params, "scriptId");
    if (!idItem) return cJSON_CreateObject();

    std::string scriptId = idItem->valuestring;
    auto it = _scriptIdToSource.find(scriptId);
    if (it == _scriptIdToSource.end()) return cJSON_CreateObject();

    cJSON* result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "scriptSource", it->second.c_str());
    return result;
}

cJSON* JsDebugBridge::runtimeEnable() {
    _runtimeEnabled = true;

    cJSON* ctxParams = cJSON_CreateObject();
    cJSON* context = cJSON_CreateObject();
    cJSON_AddNumberToObject(context, "id", 1);
    cJSON_AddStringToObject(context, "origin", "");
    cJSON_AddStringToObject(context, "name", "noix-engine");
    cJSON_AddStringToObject(context, "uniqueId", "1.1");
    cJSON* auxData = cJSON_CreateObject();
    cJSON_AddBoolToObject(auxData, "isDefault", true);
    cJSON_AddStringToObject(auxData, "type", "default");
    cJSON_AddStringToObject(auxData, "frameId", "1");
    cJSON_AddItemToObject(context, "auxData", auxData);
    cJSON_AddItemToObject(ctxParams, "context", context);
    pushEvent("Runtime.executionContextCreated", ctxParams);

    if (_debugWait && !_debuggerReady.load()) {
        pushEvent("Runtime.waitingForDebugger", cJSON_CreateObject());
    }

    return cJSON_CreateObject();
}

cJSON* JsDebugBridge::runtimeEvaluate(const cJSON* params) {
    // Must run on script thread — calls JS_Eval
    const cJSON* exprItem = cJSON_GetObjectItem(params, "expression");
    if (!exprItem) return cJSON_CreateObject();

    std::string expr = exprItem->valuestring;

    return enqueueAndWait([this, expr]() -> cJSON* {
        JSValue val = JS_Eval(_ctx, expr.c_str(), expr.size(), "<eval>",
                               JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_DEBUG_INFO);

        cJSON* result = cJSON_CreateObject();
        cJSON* ro = jsValueToRemoteObject(_ctx, val);
        cJSON_AddItemToObject(result, "result", ro);

        JS_FreeValue(_ctx, val);
        return result;
    });
}

cJSON* JsDebugBridge::runtimeCallFunctionOn(const cJSON* params) {
    // Chrome Console uses this to evaluate expressions.
    // The functionDeclaration is typically: "function() { return <expr> }"
    // With arguments: "function(arg) { return arg * 2 }" + arguments: [{value: 21}]
    const cJSON* fnItem = cJSON_GetObjectItem(params, "functionDeclaration");
    if (!fnItem || !fnItem->valuestring) return cJSON_CreateObject();

    std::string fn = fnItem->valuestring;
    const cJSON* argsItem = cJSON_GetObjectItem(params, "arguments");

    // If there are arguments, build an IIFE with argument values
    if (argsItem && cJSON_IsArray(argsItem) && cJSON_GetArraySize(argsItem) > 0) {
        std::string iife = "(" + fn + ")(";
        int argCount = cJSON_GetArraySize(argsItem);
        for (int i = 0; i < argCount; ++i) {
            if (i > 0) iife += ", ";
            cJSON* arg = cJSON_GetArrayItem(argsItem, i);
            const cJSON* valItem = cJSON_GetObjectItem(arg, "value");
            if (!valItem) {
                // objectId or unhandled — pass undefined
                iife += "undefined";
            } else if (cJSON_IsNumber(valItem)) {
                double d = valItem->valuedouble;
                if (d == static_cast<int64_t>(d)) {
                    iife += std::to_string(static_cast<int64_t>(d));
                } else {
                    iife += std::to_string(d);
                }
            } else if (cJSON_IsString(valItem)) {
                iife += "\"";
                for (const char* s = valItem->valuestring; *s; ++s) {
                    if (*s == '"' || *s == '\\') iife += '\\';
                    iife += *s;
                }
                iife += "\"";
            } else if (cJSON_IsBool(valItem)) {
                iife += valItem->type == cJSON_True ? "true" : "false";
            } else {
                iife += "undefined";
            }
        }
        iife += ")";

        return enqueueAndWait([this, iife]() -> cJSON* {
            JSValue val = JS_Eval(_ctx, iife.c_str(), iife.size(), "<console>",
                                   JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_DEBUG_INFO);

            cJSON* result = cJSON_CreateObject();
            cJSON* ro = jsValueToRemoteObject(_ctx, val);
            cJSON_AddItemToObject(result, "result", ro);

            if (JS_IsException(val)) {
                JSValue e = JS_GetException(_ctx);
                cJSON* excDetail = cJSON_CreateObject();
                cJSON_AddNumberToObject(excDetail, "exceptionId", 1);
                const char* msg = JS_ToCString(_ctx, e);
                cJSON* text = cJSON_CreateObject();
                cJSON_AddStringToObject(text, "type", "string");
                cJSON_AddStringToObject(text, "value", msg ? msg : "Error");
                cJSON_AddItemToObject(excDetail, "text", text);
                cJSON_AddItemToObject(result, "exceptionDetails", excDetail);
                JS_FreeCString(_ctx, msg);
                JS_FreeValue(_ctx, e);
            }

            JS_FreeValue(_ctx, val);
            return result;
        });
    }

    // No arguments: extract expression from "function() { return <expr> }"
    std::string expr;
    size_t returnPos = fn.find("return ");
    if (returnPos != std::string::npos) {
        size_t exprStart = returnPos + 7;  // skip "return "
        // Find the closing brace
        size_t bracePos = fn.rfind('}');
        if (bracePos != std::string::npos && bracePos > exprStart) {
            expr = fn.substr(exprStart, bracePos - exprStart);
            // Trim whitespace and semicolons
            while (!expr.empty() && (expr.back() == ' ' || expr.back() == '\n' ||
                   expr.back() == '\r' || expr.back() == '\t' || expr.back() == ';')) {
                expr.pop_back();
            }
            while (!expr.empty() && (expr.front() == ' ' || expr.front() == '\n' ||
                   expr.front() == '\r' || expr.front() == '\t')) {
                expr.erase(expr.begin());
            }
        }
    }

    // Fallback: evaluate the whole function declaration as an IIFE
    if (expr.empty()) {
        expr = "(" + fn + ")()";
    }

    return enqueueAndWait([this, expr]() -> cJSON* {
        JSValue val = JS_Eval(_ctx, expr.c_str(), expr.size(), "<console>",
                               JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_DEBUG_INFO);

        cJSON* result = cJSON_CreateObject();
        cJSON* ro = jsValueToRemoteObject(_ctx, val);
        cJSON_AddItemToObject(result, "result", ro);

        if (JS_IsException(val)) {
            JSValue e = JS_GetException(_ctx);
            cJSON* excDetail = cJSON_CreateObject();
            cJSON_AddNumberToObject(excDetail, "exceptionId", 1);
            const char* msg = JS_ToCString(_ctx, e);
            cJSON* text = cJSON_CreateObject();
            cJSON_AddStringToObject(text, "type", "string");
            cJSON_AddStringToObject(text, "value", msg ? msg : "Error");
            cJSON_AddItemToObject(excDetail, "text", text);
            cJSON_AddItemToObject(result, "exceptionDetails", excDetail);
            JS_FreeCString(_ctx, msg);
            JS_FreeValue(_ctx, e);
        }

        JS_FreeValue(_ctx, val);
        return result;
    });
}

// ---- Helpers ----

cJSON* JsDebugBridge::jsValueToRemoteObject(JSContext* ctx, JSValueConst val) {
    cJSON* obj = cJSON_CreateObject();

    const char* type = "undefined";
    const char* subtype = nullptr;
    std::string valueStr;
    std::string description;

    if (JS_IsNumber(val)) {
        type = "number";
        double d;
        JS_ToFloat64(ctx, &d, val);
        description = std::to_string(d);
        if (description.find('.') != std::string::npos) {
            description.erase(description.find_last_not_of('0') + 1);
            if (description.back() == '.') description.pop_back();
        }
        valueStr = description;
    } else if (JS_IsBool(val)) {
        type = "boolean";
        valueStr = JS_ToBool(ctx, val) ? "true" : "false";
        description = valueStr;
    } else if (JS_IsString(val)) {
        type = "string";
        const char* s = JS_ToCString(ctx, val);
        valueStr = s ? s : "";
        description = valueStr;
        JS_FreeCString(ctx, s);
    } else if (JS_IsNull(val)) {
        type = "object";
        subtype = "null";
        description = "null";
    } else if (JS_IsUndefined(val)) {
        type = "undefined";
        description = "undefined";
    } else if (JS_IsFunction(ctx, val)) {
        type = "function";
        description = "function";
    } else if (JS_IsObject(val)) {
        type = "object";
        description = "Object";
    } else {
        type = "undefined";
        description = "unknown";
    }

    cJSON_AddStringToObject(obj, "type", type);
    if (subtype) cJSON_AddStringToObject(obj, "subtype", subtype);
    cJSON_AddStringToObject(obj, "description", description.c_str());

    if (type == std::string("number")) {
        cJSON_AddRawToObject(obj, "value", valueStr.c_str());
    } else if (type == std::string("boolean")) {
        cJSON_AddBoolToObject(obj, "value", valueStr == "true");
    } else if (type == std::string("string")) {
        cJSON_AddStringToObject(obj, "value", valueStr.c_str());
    }

    return obj;
}

cJSON* JsDebugBridge::buildCallFrames() {
    cJSON* arr = cJSON_CreateArray();
    if (!_rt) return arr;

    JSDebugFrameInfo* frames = nullptr;
    int count = JS_DebugCaptureStack(_rt, &frames);
    if (count <= 0 || !frames) return arr;

    for (int i = 0; i < count; ++i) {
        cJSON* frame = cJSON_CreateObject();

        std::string fid = "frame" + std::to_string(i);
        cJSON_AddStringToObject(frame, "callFrameId", fid.c_str());

        const char* fn = frames[i].func_name ? frames[i].func_name : "";
        cJSON_AddStringToObject(frame, "functionName", fn);

        cJSON* loc = cJSON_CreateObject();
        std::string sid = "0";
        auto it = _filenameToId.find(frames[i].filename ? frames[i].filename : "");
        if (it != _filenameToId.end()) sid = it->second;
        cJSON_AddStringToObject(loc, "scriptId", sid.c_str());
        cJSON_AddNumberToObject(loc, "lineNumber",
                                frames[i].line > 0 ? frames[i].line - 1 : 0);
        cJSON_AddNumberToObject(loc, "columnNumber", frames[i].col);
        cJSON_AddItemToObject(frame, "location", loc);

        // scopeChain: local scope + global scope (both required by CDP)
        cJSON* scopeChain = cJSON_CreateArray();

        // Local scope
        cJSON* localScope = cJSON_CreateObject();
        cJSON_AddStringToObject(localScope, "type", "local");
        cJSON* localObj = cJSON_CreateObject();
        cJSON_AddStringToObject(localObj, "type", "object");
        cJSON_AddStringToObject(localObj, "description", "Object");
        cJSON_AddStringToObject(localObj, "objectId", ("scope:" + fid).c_str());
        cJSON_AddItemToObject(localScope, "object", localObj);
        cJSON* localScopeLoc = cJSON_CreateObject();
        cJSON_AddStringToObject(localScopeLoc, "scriptId", sid.c_str());
        cJSON_AddNumberToObject(localScopeLoc, "lineNumber",
                                frames[i].line > 0 ? frames[i].line - 1 : 0);
        cJSON_AddItemToObject(localScope, "startLocation", localScopeLoc);
        cJSON_AddItemToArray(scopeChain, localScope);

        // Global scope
        cJSON* globalScope = cJSON_CreateObject();
        cJSON_AddStringToObject(globalScope, "type", "global");
        cJSON* globalObj = cJSON_CreateObject();
        cJSON_AddStringToObject(globalObj, "type", "object");
        cJSON_AddStringToObject(globalObj, "description", "global");
        cJSON_AddStringToObject(globalObj, "objectId", "global:0");
        cJSON_AddItemToObject(globalScope, "object", globalObj);
        cJSON_AddItemToArray(scopeChain, globalScope);

        cJSON_AddItemToObject(frame, "scopeChain", scopeChain);

        // this object (required by CDP)
        cJSON* thisObj = cJSON_CreateObject();
        cJSON_AddStringToObject(thisObj, "type", "object");
        cJSON_AddStringToObject(thisObj, "description", "global");
        cJSON_AddItemToObject(frame, "this", thisObj);

        cJSON_AddItemToArray(arr, frame);
    }

    JS_DebugFreeFrameInfo(_rt, frames, count);
    return arr;
}

} // namespace noix::debug
