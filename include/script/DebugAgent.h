#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace noix::script {

enum class DebugState { Running, Paused, Stepping };
enum class StepKind { Into, Over, Out };

struct Breakpoint {
    uint32_t id;
    std::string source;
    int line;
    bool enabled = true;
    std::string condition;  // empty = unconditional
};

struct DebugVariable {
    std::string name;
    std::string value;      // string representation
    std::string type;       // "number", "string", "boolean", "object", "function", "undefined", "null", etc.
    bool isArgument = false;
    bool isConst = false;
    bool isLexical = false;
    bool isCaptured = false;
};

struct StackFrame {
    std::string source;
    int line = 0;
    int column = 0;
    std::string funcName;
    bool native = false;
    std::vector<DebugVariable> locals;
};

class ScriptEngine;

class DebugAgent {
public:
    explicit DebugAgent(ScriptEngine& engine);
    ~DebugAgent();

    DebugAgent(const DebugAgent&) = delete;
    DebugAgent& operator=(const DebugAgent&) = delete;

    /// State query (atomic, thread-safe)
    DebugState state() const { return _state.load(); }
    bool isPaused() const { return _state.load() == DebugState::Paused; }

    /// Breakpoint management (called on script thread)
    uint32_t addBreakpoint(const std::string& source, int line,
                           const std::string& condition = "");
    bool removeBreakpoint(uint32_t id);
    void clearBreakpoints();
    std::vector<Breakpoint> listBreakpoints() const;

    /// Pause/continue/step (via postTask on script thread)
    void pause();
    void continueRun();
    void step(StepKind kind);

    /// Diagnostic: check if pause has been requested
    bool isPauseRequested() const { return _pauseRequested.load(); }

    /// Inspection (called on script thread while paused)
    std::vector<StackFrame> captureStackTrace();
    std::string evaluateInContext(const std::string& expr);
    std::string evaluateInFrame(int frameIndex, const std::string& expr);

    /// Paused loop (called on script thread)
    void runPausedLoop();

    /// SDL event type setup
    void setFreezeEventType(uint32_t type) { _freezeEventType = type; }
    void setResumeEventType(uint32_t type) { _resumeEventType = type; }

    /// Access paused state (thread-safe after capture)
    const std::string& pausedSource() const { return _pausedSource; }
    int pausedLine() const { return _pausedLine; }
    const std::vector<StackFrame>& pausedStack() const { return _pausedStack; }

private:
    void pushFreezeEvent();
    void pushResumeEvent();

    void transitionTo(DebugState newState);

    ScriptEngine& _engine;

    std::atomic<DebugState> _state{DebugState::Running};
    std::atomic<bool> _pauseRequested{false};

    // Step state
    StepKind _stepKind{StepKind::Into};
    int _stepTargetDepth{0};

    // Breakpoints
    std::vector<Breakpoint> _breakpoints;
    uint32_t _nextBreakpointId{1};

    // Paused loop
    std::mutex _pauseMutex;
    std::condition_variable _pauseCv;

    // SDL events
    uint32_t _freezeEventType{0};
    uint32_t _resumeEventType{0};

    // State captured in interrupt handler
    std::vector<StackFrame> _pausedStack;
    std::string _pausedSource;
    int _pausedLine{0};

    // Guard against re-entrant pause while in the paused loop
    std::atomic<bool> _inPausedLoop{false};
};

} // namespace noix::script
