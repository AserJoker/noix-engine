#include "debug/DebugEvalCommand.h"
#include "script/ScriptEngine.h"
#include "script/DebugAgent.h"
#include <cJSON.h>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace noix::debug {

DebugEvalCommand::DebugEvalCommand(script::ScriptEngine& engine)
    : _engine(engine) {}

std::string DebugEvalCommand::execute(const std::string& arguments) {
    cJSON* args = cJSON_Parse(arguments.c_str());
    if (!args) return "error: invalid arguments";

    cJSON* exprItem = cJSON_GetObjectItem(args, "expr");
    if (!exprItem || !cJSON_IsString(exprItem)) {
        cJSON_Delete(args);
        return "error: missing 'expr' field";
    }

    std::string expr = exprItem->valuestring;
    cJSON_Delete(args);

    std::mutex mtx;
    std::condition_variable cv;
    std::string result;
    bool done = false;

    _engine.postTask([&]() {
        auto* agent = _engine.debugAgent();
        if (agent && agent->isPaused()) {
            result = agent->evaluateInContext(expr);
        } else {
            result = "error: not paused";
        }
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    cv.wait_for(lock, std::chrono::seconds(5), [&] { return done; });

    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "result", result.c_str());
    char* str = cJSON_PrintUnformatted(json);
    std::string ret = str;
    cJSON_free(str);
    cJSON_Delete(json);
    return ret;
}

} // namespace noix::debug
