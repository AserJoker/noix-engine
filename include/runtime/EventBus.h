#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <memory>

namespace noix::core { class Value; }
namespace noix::script { class ScriptEngine; }

struct JSContext;

namespace noix::runtime {

class EventBus {
public:
    EventBus();
    ~EventBus();

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    /// Set the SDL custom event type (call once during init)
    void setEventType(uint32_t eventType);

    /// Set the ScriptEngine pointer (call once during init)
    void setScriptEngine(script::ScriptEngine* engine);

    /// Register a JS callback for an event name. Returns a handle for unsubscription.
    /// Called from script thread. JS_DupValue must be called before passing callbackPtr.
    int addEventListener(const std::string& eventName, JSContext* ctx, void* callbackPtr);

    /// Unregister a listener by handle. Thread-safe.
    /// JSValue is freed on the script thread via postTask.
    void removeEventListener(int handle);

    /// Async emit: push an SDL custom event. Safe from any thread.
    void emitAsync(const std::string& eventName, const core::Value& data);

    /// Sync emit: blocks the calling thread until all matching JS callbacks
    /// have been invoked on the script thread. Must NOT be called from the script thread.
    core::Value emitSync(const std::string& eventName, const core::Value& data);

    /// Called from the main thread's SDL event loop when a custom event is polled.
    void handleSdlEvent(const SDL_Event& event);

    /// Release all JS callback references. Must be called on the script thread
    /// during shutdown.
    void releaseAllListeners();

private:
    struct ListenerEntry {
        int handle;
        std::string eventName;
        JSContext* ctx;
        void* callbackPtr;  // JS_VALUE_GET_PTR stored as void*
    };

    struct EventPayload;  // defined in .cpp

    void dispatchToListeners(const std::string& eventName, const core::Value& data);

    script::ScriptEngine* _engine = nullptr;
    uint32_t _eventType = 0;
    std::atomic<int> _nextHandle{1};
    std::map<int, ListenerEntry> _listeners;
    std::map<std::string, std::vector<int>> _listenersByName;
    std::mutex _listenersMutex;
};

} // namespace noix::runtime
