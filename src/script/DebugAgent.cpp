#include "script/DebugAgent.h"
#include "script/ScriptEngine.h"
#include "core/Logger.h"
#include <SDL3/SDL.h>

namespace noix::script {

DebugAgent::DebugAgent(ScriptEngine& engine)
    : _engine(engine) {}

DebugAgent::~DebugAgent() = default;

// ---- State transition ----

void DebugAgent::transitionTo(DebugState newState) {
    _state.store(newState);
}

// ---- Pause/continue/step ----

void DebugAgent::pause() {
    _pauseRequested.store(true);
    // TODO: 触发 JS 引擎暂停
}

void DebugAgent::continueRun() {
    _pauseRequested.store(false);
    transitionTo(DebugState::Running);
    {
        std::lock_guard lock(_pauseMutex);
    }
    _pauseCv.notify_one();
}

void DebugAgent::step(StepKind kind) {
    _stepKind = kind;
    transitionTo(DebugState::Stepping);
    // TODO: 触发 JS 引擎单步
    {
        std::lock_guard lock(_pauseMutex);
    }
    _pauseCv.notify_one();
}

// ---- Paused loop ----

void DebugAgent::runPausedLoop() {
    _inPausedLoop.store(true);
    pushFreezeEvent();

    core::Logger::instance().info("DebugAgent paused at {}:{}",
        _pausedSource, _pausedLine);

    while (_state.load() == DebugState::Paused) {
        _engine.drainTaskQueue();

        std::unique_lock lock(_pauseMutex);
        _pauseCv.wait_for(lock, std::chrono::milliseconds(50),
            [this] { return _state.load() != DebugState::Paused; });
    }

    core::Logger::instance().info("DebugAgent resumed");
    pushResumeEvent();
    _inPausedLoop.store(false);
}

// ---- Breakpoint management ----

uint32_t DebugAgent::addBreakpoint(const std::string& source, int line,
                                    const std::string& condition) {
    uint32_t id = _nextBreakpointId++;
    _breakpoints.push_back({id, source, line, true, condition});

    core::Logger::instance().info("Breakpoint added: #{} at {}:{}{}",
        id, source, line,
        condition.empty() ? "" : " condition=\"" + condition + "\"");

    // TODO: 在 JS 引擎中设置断点
    return id;
}

bool DebugAgent::removeBreakpoint(uint32_t id) {
    for (auto it = _breakpoints.begin(); it != _breakpoints.end(); ++it) {
        if (it->id == id) {
            _breakpoints.erase(it);
            // TODO: 在 JS 引擎中移除断点
            return true;
        }
    }
    return false;
}

void DebugAgent::clearBreakpoints() {
    _breakpoints.clear();
    // TODO: 在 JS 引擎中清除断点
}

std::vector<Breakpoint> DebugAgent::listBreakpoints() const {
    return _breakpoints;
}

// ---- Stack trace capture ----

std::vector<StackFrame> DebugAgent::captureStackTrace() {
    // TODO: 从 JS 引擎获取调用栈
    return _pausedStack;
}

// ---- Expression evaluation ----

std::string DebugAgent::evaluateInContext(const std::string& expr) {
    // TODO: 在 JS 引擎中求值
    return "error: script engine not available";
}

std::string DebugAgent::evaluateInFrame(int frameIndex, const std::string& expr) {
    // TODO: 在指定帧上下文中求值
    return "error: script engine not available";
}

// ---- SDL events ----

void DebugAgent::pushFreezeEvent() {
    if (_freezeEventType == 0) return;
    SDL_Event event;
    SDL_zero(event);
    event.type = _freezeEventType;
    SDL_PushEvent(&event);
}

void DebugAgent::pushResumeEvent() {
    if (_resumeEventType == 0) return;
    SDL_Event event;
    SDL_zero(event);
    event.type = _resumeEventType;
    SDL_PushEvent(&event);
}

} // namespace noix::script
