#include "debug/BreakpointCommand.h"
#include "script/ScriptEngine.h"
#include "script/DebugAgent.h"
#include <cJSON.h>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace noix::debug {

BreakpointSetCommand::BreakpointSetCommand(script::ScriptEngine& engine)
    : _engine(engine) {}

std::string BreakpointSetCommand::execute(const std::string& arguments) {
    cJSON* args = cJSON_Parse(arguments.c_str());
    if (!args) return "error: invalid arguments";

    cJSON* sourceItem = cJSON_GetObjectItem(args, "source");
    cJSON* lineItem = cJSON_GetObjectItem(args, "line");

    if (!sourceItem || !cJSON_IsString(sourceItem) ||
        !lineItem || !cJSON_IsNumber(lineItem)) {
        cJSON_Delete(args);
        return "error: missing 'source' or 'line' field";
    }

    std::string source = sourceItem->valuestring;
    int line = lineItem->valueint;

    cJSON* condItem = cJSON_GetObjectItem(args, "condition");
    std::string condition = (condItem && cJSON_IsString(condItem)) ? condItem->valuestring : "";
    cJSON_Delete(args);

    std::mutex mtx;
    std::condition_variable cv;
    uint32_t bpId = 0;
    bool done = false;

    _engine.postTask([&]() {
        auto* agent = _engine.debugAgent();
        if (agent) {
            bpId = agent->addBreakpoint(source, line, condition);
        }
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    cv.wait_for(lock, std::chrono::seconds(2), [&] { return done; });

    cJSON* result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "id", bpId);
    char* json = cJSON_PrintUnformatted(result);
    std::string ret = json;
    cJSON_free(json);
    cJSON_Delete(result);
    return ret;
}

BreakpointRemoveCommand::BreakpointRemoveCommand(script::ScriptEngine& engine)
    : _engine(engine) {}

std::string BreakpointRemoveCommand::execute(const std::string& arguments) {
    cJSON* args = cJSON_Parse(arguments.c_str());
    if (!args) return "error: invalid arguments";

    cJSON* idItem = cJSON_GetObjectItem(args, "id");
    if (!idItem || !cJSON_IsNumber(idItem)) {
        cJSON_Delete(args);
        return "error: missing 'id' field";
    }

    uint32_t id = static_cast<uint32_t>(idItem->valueint);
    cJSON_Delete(args);

    std::mutex mtx;
    std::condition_variable cv;
    bool removed = false;
    bool done = false;

    _engine.postTask([&]() {
        auto* agent = _engine.debugAgent();
        if (agent) {
            removed = agent->removeBreakpoint(id);
        }
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    cv.wait_for(lock, std::chrono::seconds(2), [&] { return done; });
    return removed ? "ok" : "error: breakpoint not found";
}

BreakpointClearCommand::BreakpointClearCommand(script::ScriptEngine& engine)
    : _engine(engine) {}

std::string BreakpointClearCommand::execute(const std::string&) {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;

    _engine.postTask([&]() {
        auto* agent = _engine.debugAgent();
        if (agent) {
            agent->clearBreakpoints();
        }
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    cv.wait_for(lock, std::chrono::seconds(2), [&] { return done; });
    return "ok";
}

BreakpointListCommand::BreakpointListCommand(script::ScriptEngine& engine)
    : _engine(engine) {}

std::string BreakpointListCommand::execute(const std::string&) {
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<script::Breakpoint> breakpoints;
    bool done = false;

    _engine.postTask([&]() {
        auto* agent = _engine.debugAgent();
        if (agent) {
            breakpoints = agent->listBreakpoints();
        }
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    cv.wait_for(lock, std::chrono::seconds(2), [&] { return done; });

    cJSON* arr = cJSON_CreateArray();
    for (const auto& bp : breakpoints) {
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "id", bp.id);
        cJSON_AddStringToObject(obj, "source", bp.source.c_str());
        cJSON_AddNumberToObject(obj, "line", bp.line);
        cJSON_AddBoolToObject(obj, "enabled", bp.enabled);
        if (!bp.condition.empty()) {
            cJSON_AddStringToObject(obj, "condition", bp.condition.c_str());
        }
        cJSON_AddItemToArray(arr, obj);
    }

    char* json = cJSON_PrintUnformatted(arr);
    std::string ret = json;
    cJSON_free(json);
    cJSON_Delete(arr);
    return ret;
}

} // namespace noix::debug
