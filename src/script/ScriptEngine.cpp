#include "script/ScriptEngine.h"
#include "debug/DapBridge.h"
#include "core/Logger.h"

#include "quickjs.h"
#include <SDL3/SDL.h>

namespace noix::script {

ScriptEngine::ScriptEngine(const std::string& basePath)
    : _scriptsPath(basePath + "/scripts") {}

ScriptEngine::~ScriptEngine() {
    stop();
}

void ScriptEngine::start() {
    if (_running.load()) return;
    _running.store(true);
    _thread = std::thread(&ScriptEngine::scriptThreadFunc, this);
    core::Logger::instance().info("ScriptEngine started");
}

void ScriptEngine::stop() {
    if (!_running.load()) return;
    _running.store(false);
    {
        std::lock_guard lock(_queueMutex);
        _queueCv.notify_one();
    }
    if (_thread.joinable()) {
        _thread.join();
    }
    core::Logger::instance().info("ScriptEngine stopped");
}

void ScriptEngine::postTask(std::function<void()> task) {
    {
        std::lock_guard lock(_queueMutex);
        _taskQueue.push(std::move(task));
    }
    _queueCv.notify_one();
}

void ScriptEngine::drainTaskQueue() {
    while (true) {
        std::function<void()> task;
        {
            std::lock_guard lock(_queueMutex);
            if (_taskQueue.empty()) break;
            task = std::move(_taskQueue.front());
            _taskQueue.pop();
        }
        if (task) {
            task();
        }
    }
}

void ScriptEngine::setDapBridge(debug::DapBridge* bridge) {
    _dapBridge = bridge;
}

void ScriptEngine::setDebugEventTypes(uint32_t freezeType, uint32_t resumeType) {
    _freezeEventType = freezeType;
    _resumeEventType = resumeType;
}

void ScriptEngine::scriptThreadFunc() {
    /* Create QuickJS runtime and context */
    _rt = JS_NewRuntime();
    _ctx = JS_NewContext(_rt);

    if (!_rt || !_ctx) {
        core::Logger::instance().error("ScriptEngine: failed to create QuickJS runtime");
        return;
    }

    /* Wire up DAP bridge if present */
    if (_dapBridge) {
        _dapBridge->rt = _rt;
        _dapBridge->ctx = _ctx;

        JS_SetDebugCallback(_rt, debug::DapBridge::debugCallback, _dapBridge);
        JS_SetDebugDrainQueue(_rt, debug::DapBridge::drainQueue);

        _dapBridge->setDebugEventTypes(_freezeEventType, _resumeEventType);
    }

    core::Logger::instance().info("ScriptEngine: QuickJS runtime initialized");

    /* Wait for DAP launch signal or process tasks */
    while (_running.load()) {
        /* Check if DAP launch was requested */
        if (_dapBridge && _dapBridge->launchRequested.load()) {
            _dapBridge->launchRequested = false;
            _dapBridge->executeScript();
            /* After script completes, continue task loop */
        }

        std::function<void()> task;
        {
            std::unique_lock lock(_queueMutex);
            _queueCv.wait_for(lock, std::chrono::milliseconds(100),
                [this] { return !_taskQueue.empty() || !_running.load(); });
            if (!_taskQueue.empty()) {
                task = std::move(_taskQueue.front());
                _taskQueue.pop();
            }
        }
        if (task) {
            task();
        }
    }

    /* Cleanup QuickJS */
    if (_dapBridge) {
        _dapBridge->rt = nullptr;
        _dapBridge->ctx = nullptr;
    }
    if (_ctx) { JS_FreeContext(_ctx); _ctx = nullptr; }
    if (_rt) { JS_RunGC(_rt); JS_FreeRuntime(_rt); _rt = nullptr; }
}

} // namespace noix::script
