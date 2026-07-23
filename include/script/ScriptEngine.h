#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace noix::script {

class DebugAgent;

class ScriptEngine {
public:
    ScriptEngine(const std::string& basePath);
    ~ScriptEngine();

    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    /// 启动脚本线程
    void start();

    /// 优雅停止脚本线程
    void stop();

    /// 向脚本线程投递任务（线程安全）
    void postTask(std::function<void()> task);

    /// 排空任务队列（脚本线程调用，用于暂停循环中处理命令）
    void drainTaskQueue();

    /// 获取 DebugAgent（仅脚本线程访问）
    DebugAgent* debugAgent() const { return _debugAgent.get(); }

    /// 获取脚本目录路径
    const std::string& scriptsPath() const { return _scriptsPath; }

    /// 设置调试冻结/恢复 SDL 事件类型
    void setDebugEventTypes(uint32_t freezeType, uint32_t resumeType);

    /// 设置 debug-wait 模式：start() 后等待 debugRun() 才加载脚本
    void setDebugWait(bool wait) { _debugWait = wait; }

    /// 通知脚本线程可以开始加载脚本（配合 --debug-wait）
    void debugRun();

private:
    void scriptThreadFunc();

    std::string _scriptsPath;

    std::thread _thread;
    std::mutex _queueMutex;
    std::condition_variable _queueCv;
    std::queue<std::function<void()>> _taskQueue;
    std::atomic<bool> _running{false};

    // 仅脚本线程访问
    std::unique_ptr<DebugAgent> _debugAgent;

    // 调试事件类型（在 start 前设置）
    uint32_t _freezeEventType{0};
    uint32_t _resumeEventType{0};

    // debug-wait 模式
    bool _debugWait{false};
    std::atomic<bool> _debugRunSignaled{false};
};

} // namespace noix::script
