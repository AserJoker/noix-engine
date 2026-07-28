#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

struct JSRuntime;
struct JSContext;

namespace noix::debug { class DapBridge; }

namespace noix::script {

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

    /// 获取脚本目录路径
    const std::string& scriptsPath() const { return _scriptsPath; }

    /// 设置 DAP bridge（在 start 前调用）
    void setDapBridge(debug::DapBridge* bridge);

    /// 获取 DAP bridge
    debug::DapBridge* dapBridge() const { return _dapBridge; }

    /// 设置调试冻结/恢复 SDL 事件类型
    void setDebugEventTypes(uint32_t freezeType, uint32_t resumeType);

    /// QuickJS runtime/context 访问（仅脚本线程）
    JSRuntime* runtime() const { return _rt; }
    JSContext* context() const { return _ctx; }

private:
    void scriptThreadFunc();

    std::string _scriptsPath;

    std::thread _thread;
    std::mutex _queueMutex;
    std::condition_variable _queueCv;
    std::queue<std::function<void()>> _taskQueue;
    std::atomic<bool> _running{false};

    /// QuickJS runtime（脚本线程所有）
    JSRuntime* _rt = nullptr;
    JSContext* _ctx = nullptr;

    /// DAP bridge
    debug::DapBridge* _dapBridge = nullptr;

    // 调试事件类型（在 start 前设置）
    uint32_t _freezeEventType{0};
    uint32_t _resumeEventType{0};
};

} // namespace noix::script
