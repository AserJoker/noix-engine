#include "debug/DebugControlCommand.h"
#include "script/ScriptEngine.h"
#include "script/DebugAgent.h"
#include <cJSON.h>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace noix::debug {

PauseCommand::PauseCommand(script::ScriptEngine& engine)
    : _engine(engine) {}

std::string PauseCommand::execute(const std::string&) {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;

    _engine.postTask([&]() {
        auto* agent = _engine.debugAgent();
        if (agent) {
            agent->pause();
        }
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    cv.wait_for(lock, std::chrono::seconds(2), [&] { return done; });
    return "ok";
}

ContinueCommand::ContinueCommand(script::ScriptEngine& engine)
    : _engine(engine) {}

std::string ContinueCommand::execute(const std::string&) {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;

    _engine.postTask([&]() {
        auto* agent = _engine.debugAgent();
        if (agent) {
            agent->continueRun();
        }
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    cv.wait_for(lock, std::chrono::seconds(2), [&] { return done; });
    return "ok";
}

StepCommand::StepCommand(script::ScriptEngine& engine)
    : _engine(engine) {}

std::string StepCommand::execute(const std::string& arguments) {
    std::string kindStr = "into";
    if (!arguments.empty() && arguments != "{}") {
        cJSON* args = cJSON_Parse(arguments.c_str());
        if (args) {
            cJSON* kind = cJSON_GetObjectItem(args, "kind");
            if (kind && cJSON_IsString(kind)) {
                kindStr = kind->valuestring;
            }
            cJSON_Delete(args);
        }
    }

    script::StepKind kind = script::StepKind::Into;
    if (kindStr == "over") kind = script::StepKind::Over;
    else if (kindStr == "out") kind = script::StepKind::Out;

    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;

    _engine.postTask([this, kind, &mtx, &cv, &done]() {
        auto* agent = _engine.debugAgent();
        if (agent) {
            agent->step(kind);
        }
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    cv.wait_for(lock, std::chrono::seconds(2), [&] { return done; });
    return "ok";
}

} // namespace noix::debug
