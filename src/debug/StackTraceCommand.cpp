#include "debug/StackTraceCommand.h"
#include "script/ScriptEngine.h"
#include "script/DebugAgent.h"
#include <cJSON.h>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace noix::debug {

StackTraceCommand::StackTraceCommand(script::ScriptEngine& engine)
    : _engine(engine) {}

std::string StackTraceCommand::execute(const std::string&) {
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<script::StackFrame> frames;
    bool done = false;

    _engine.postTask([&]() {
        auto* agent = _engine.debugAgent();
        if (agent && agent->isPaused()) {
            frames = agent->pausedStack();
        }
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    cv.wait_for(lock, std::chrono::seconds(2), [&] { return done; });

    cJSON* arr = cJSON_CreateArray();
    for (const auto& frame : frames) {
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "source", frame.source.c_str());
        cJSON_AddNumberToObject(obj, "line", frame.line);
        cJSON_AddNumberToObject(obj, "column", frame.column);
        cJSON_AddStringToObject(obj, "funcName", frame.funcName.c_str());
        cJSON_AddBoolToObject(obj, "native", frame.native);
        cJSON_AddItemToArray(arr, obj);
    }

    char* json = cJSON_PrintUnformatted(arr);
    std::string ret = json;
    cJSON_free(json);
    cJSON_Delete(arr);
    return ret;
}

} // namespace noix::debug
