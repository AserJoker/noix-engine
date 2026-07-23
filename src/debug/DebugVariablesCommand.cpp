#include "debug/DebugVariablesCommand.h"
#include "script/ScriptEngine.h"
#include "script/DebugAgent.h"
#include <cJSON.h>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace noix::debug {

DebugVariablesCommand::DebugVariablesCommand(script::ScriptEngine& engine)
    : _engine(engine) {}

std::string DebugVariablesCommand::execute(const std::string& arguments) {
    cJSON* args = cJSON_Parse(arguments.c_str());
    if (!args) return "error: invalid arguments";

    cJSON* frameItem = cJSON_GetObjectItem(args, "frameIndex");
    int frameIndex = frameItem && cJSON_IsNumber(frameItem) ? frameItem->valueint : 0;
    cJSON_Delete(args);

    std::mutex mtx;
    std::condition_variable cv;
    std::vector<script::DebugVariable> variables;
    bool done = false;

    _engine.postTask([&]() {
        auto* agent = _engine.debugAgent();
        if (agent && agent->isPaused()) {
            const auto& stack = agent->pausedStack();
            if (frameIndex >= 0 && frameIndex < (int)stack.size()) {
                variables = stack[frameIndex].locals;
            }
        }
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    cv.wait_for(lock, std::chrono::seconds(5), [&] { return done; });

    cJSON* arr = cJSON_CreateArray();
    for (const auto& var : variables) {
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "name", var.name.c_str());
        cJSON_AddStringToObject(obj, "value", var.value.c_str());
        cJSON_AddStringToObject(obj, "type", var.type.c_str());
        cJSON_AddBoolToObject(obj, "isArgument", var.isArgument);
        cJSON_AddBoolToObject(obj, "isConst", var.isConst);
        cJSON_AddBoolToObject(obj, "isLexical", var.isLexical);
        cJSON_AddBoolToObject(obj, "isCaptured", var.isCaptured);
        cJSON_AddItemToArray(arr, obj);
    }

    char* json = cJSON_PrintUnformatted(arr);
    std::string ret = json;
    cJSON_free(json);
    cJSON_Delete(arr);
    return ret;
}

} // namespace noix::debug
