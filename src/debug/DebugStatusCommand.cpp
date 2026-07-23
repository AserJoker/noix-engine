#include "debug/DebugStatusCommand.h"
#include "script/ScriptEngine.h"
#include "script/DebugAgent.h"
#include <cJSON.h>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace noix::debug {

DebugStatusCommand::DebugStatusCommand(script::ScriptEngine& engine)
    : _engine(engine) {}

std::string DebugStatusCommand::execute(const std::string&) {
    std::mutex mtx;
    std::condition_variable cv;
    script::DebugState state = script::DebugState::Running;
    std::string source;
    int line = 0;
    bool done = false;

    _engine.postTask([&]() {
        auto* agent = _engine.debugAgent();
        if (agent) {
            state = agent->state();
            if (agent->isPaused()) {
                source = agent->pausedSource();
                line = agent->pausedLine();
            }
        }
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    cv.wait_for(lock, std::chrono::seconds(2), [&] { return done; });

    const char* stateStr = "running";
    switch (state) {
    case script::DebugState::Paused:   stateStr = "paused"; break;
    case script::DebugState::Stepping: stateStr = "stepping"; break;
    default: break;
    }

    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "state", stateStr);
    if (state == script::DebugState::Paused) {
        cJSON_AddStringToObject(json, "source", source.c_str());
        cJSON_AddNumberToObject(json, "line", line);
    }

    char* str = cJSON_PrintUnformatted(json);
    std::string ret = str;
    cJSON_free(str);
    cJSON_Delete(json);
    return ret;
}

} // namespace noix::debug
