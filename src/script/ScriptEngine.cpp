#include "script/ScriptEngine.h"
#include "script/DebugAgent.h"
#include "core/Logger.h"
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
    _debugRunSignaled = true;
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

void ScriptEngine::setDebugEventTypes(uint32_t freezeType, uint32_t resumeType) {
    _freezeEventType = freezeType;
    _resumeEventType = resumeType;
}

void ScriptEngine::debugRun() {
    _debugRunSignaled = true;
    _queueCv.notify_one();
}

// ---- 脚本线程主函数 ----

void ScriptEngine::scriptThreadFunc() {
    // TODO: 初始化 JS 引擎

    _debugAgent = std::make_unique<DebugAgent>(*this);
    _debugAgent->setFreezeEventType(_freezeEventType);
    _debugAgent->setResumeEventType(_resumeEventType);
    // TODO: _debugAgent->install()

    // debug-wait 模式
    if (_debugWait) {
        core::Logger::instance().info("Debug-wait: waiting for debug-run signal...");
        while (_running.load() && !_debugRunSignaled) {
            std::function<void()> task;
            {
                std::unique_lock lock(_queueMutex);
                _queueCv.wait_for(lock, std::chrono::milliseconds(100),
                    [this] { return !_taskQueue.empty() || !_running.load() || _debugRunSignaled; });
                if (!_taskQueue.empty()) {
                    task = std::move(_taskQueue.front());
                    _taskQueue.pop();
                }
            }
            if (task) {
                task();
            }
        }
        if (!_running.load()) { return; }
        core::Logger::instance().info("Debug-wait: signal received, loading scripts");
    }

    // TODO: 加载入口文件

    // 事件循环
    while (_running.load()) {
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

    // 清理
    _debugAgent.reset();
    // TODO: 清理 JS 引擎
}

} // namespace noix::script
