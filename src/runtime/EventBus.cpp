#include "runtime/EventBus.h"
#include "core/Logger.h"
#include "core/Value.h"
#include "script/ScriptEngine.h"
#include "quickjs.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace noix::runtime {

struct EventBus::EventPayload {
    std::string eventName;
    core::Value data;
};

EventBus::EventBus() = default;

EventBus::~EventBus() {
    std::lock_guard lock(_listenersMutex);
    if (!_listeners.empty()) {
        core::Logger::instance().warn("EventBus: destroyed with {} listeners still registered",
                                       _listeners.size());
    }
}

void EventBus::setEventType(uint32_t eventType) {
    _eventType = eventType;
}

void EventBus::setScriptEngine(script::ScriptEngine* engine) {
    _engine = engine;
}

int EventBus::addEventListener(const std::string& eventName, JSContext* ctx, void* callbackPtr) {
    int handle = _nextHandle.fetch_add(1);
    {
        std::lock_guard lock(_listenersMutex);
        _listeners[handle] = ListenerEntry{handle, eventName, ctx, callbackPtr};
        _listenersByName[eventName].push_back(handle);
    }
    return handle;
}

void EventBus::removeEventListener(int handle) {
    ListenerEntry entry;
    {
        std::lock_guard lock(_listenersMutex);
        auto it = _listeners.find(handle);
        if (it == _listeners.end()) return;
        entry = std::move(it->second);
        _listeners.erase(it);

        auto& handles = _listenersByName[entry.eventName];
        handles.erase(std::remove(handles.begin(), handles.end(), handle), handles.end());
        if (handles.empty()) _listenersByName.erase(entry.eventName);
    }

    /* Free the JSValue on the script thread */
    if (_engine) {
        auto* ctx = entry.ctx;
        auto* cb = entry.callbackPtr;
        _engine->postTask([ctx, cb]() {
            JSValue val = JS_MKPTR(JS_TAG_OBJECT, cb);
            JS_FreeValue(ctx, val);
        });
    }
}

void EventBus::emitAsync(const std::string& eventName, const core::Value& data) {
    auto* payload = new EventPayload{eventName, data};

    SDL_Event event{};
    event.type = _eventType;
    event.user.code = 0;
    event.user.data1 = payload;
    event.user.data2 = nullptr;

    if (!SDL_PushEvent(&event)) {
        delete payload;
        core::Logger::instance().warn("EventBus: SDL_PushEvent failed for '{}'", eventName);
    }
}

void EventBus::handleSdlEvent(const SDL_Event& event) {
    auto* payload = static_cast<EventPayload*>(event.user.data1);
    if (!payload) return;

    /* Take ownership: always delete, even if dispatch fails */
    std::unique_ptr<EventPayload> owned(payload);

    if (!_engine) return;

    std::string eventName = payload->eventName;
    core::Value data = payload->data;

    _engine->postTask([this, eventName = std::move(eventName), data = std::move(data)]() {
        dispatchToListeners(eventName, data);
    });
}

void EventBus::dispatchToListeners(const std::string& eventName, const core::Value& data) {
    /* Collect matching listener entries under lock, then release lock before invoking callbacks */
    std::vector<ListenerEntry> matching;
    {
        std::lock_guard lock(_listenersMutex);
        auto it = _listenersByName.find(eventName);
        if (it != _listenersByName.end()) {
            for (int handle : it->second) {
                auto lit = _listeners.find(handle);
                if (lit != _listeners.end()) {
                    matching.push_back(lit->second);
                }
            }
        }
    }

    std::string dataJson = data.dump();

    for (auto& entry : matching) {
        JSValue jsCallback = JS_MKPTR(JS_TAG_OBJECT, entry.callbackPtr);
        JSValue argv[] = {
            JS_ParseJSON(entry.ctx, dataJson.c_str(), dataJson.size(), "<eventbus>")
        };
        JSValue result = JS_Call(entry.ctx, jsCallback, JS_UNDEFINED, 1, argv);
        JS_FreeValue(entry.ctx, argv[0]);

        if (JS_IsException(result)) {
            JSValue exc = JS_GetException(entry.ctx);
            const char* err = JS_ToCString(entry.ctx, exc);
            core::Logger::instance().warn("EventBus: callback for '{}' threw: {}",
                                           eventName, err ? err : "unknown");
            if (err) JS_FreeCString(entry.ctx, err);
            JS_FreeValue(entry.ctx, exc);
        }
        JS_FreeValue(entry.ctx, result);
    }
}

core::Value EventBus::emitSync(const std::string& eventName, const core::Value& data) {
    if (!_engine) return core::Value();

    /* Collect matching listener entries */
    std::vector<ListenerEntry> matching;
    {
        std::lock_guard lock(_listenersMutex);
        auto it = _listenersByName.find(eventName);
        if (it != _listenersByName.end()) {
            for (int handle : it->second) {
                auto lit = _listeners.find(handle);
                if (lit != _listeners.end()) {
                    matching.push_back(lit->second);
                }
            }
        }
    }

    if (matching.empty()) return core::Value();

    std::mutex waitMutex;
    std::condition_variable waitCv;
    bool done = false;
    core::Value lastResult;

    _engine->postTask([&, matching = std::move(matching)]() {
        std::string dataJson = data.dump();

        for (auto& entry : matching) {
            JSValue jsCallback = JS_MKPTR(JS_TAG_OBJECT, entry.callbackPtr);
            JSValue argv[] = {
                JS_ParseJSON(entry.ctx, dataJson.c_str(), dataJson.size(), "<eventbus>")
            };
            JSValue jsResult = JS_Call(entry.ctx, jsCallback, JS_UNDEFINED, 1, argv);
            JS_FreeValue(entry.ctx, argv[0]);

            core::Value responseValue;
            if (JS_IsException(jsResult)) {
                JSValue exc = JS_GetException(entry.ctx);
                const char* err = JS_ToCString(entry.ctx, exc);
                core::Logger::instance().warn("EventBus sync: callback threw: {}",
                                               err ? err : "unknown");
                if (err) JS_FreeCString(entry.ctx, err);
                JS_FreeValue(entry.ctx, exc);
                responseValue = core::Value::object();
            } else if (!JS_IsUndefined(jsResult) && !JS_IsNull(jsResult)) {
                JSValue jsonStr = JS_JSONStringify(entry.ctx, jsResult, JS_UNDEFINED, JS_UNDEFINED);
                if (JS_IsString(jsonStr)) {
                    const char* s = JS_ToCString(entry.ctx, jsonStr);
                    if (s) {
                        responseValue = core::Value::parse(std::string(s));
                        JS_FreeCString(entry.ctx, s);
                    }
                }
                JS_FreeValue(entry.ctx, jsonStr);
                if (responseValue.isNull()) responseValue = core::Value::object();
            } else {
                responseValue = core::Value::object();
            }
            JS_FreeValue(entry.ctx, jsResult);
            lastResult = std::move(responseValue);
        }

        {
            std::lock_guard<std::mutex> lk(waitMutex);
            done = true;
        }
        waitCv.notify_one();
    });

    std::unique_lock<std::mutex> lk(waitMutex);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done && std::chrono::steady_clock::now() < deadline) {
        waitCv.wait_until(lk, deadline);
    }
    if (!done) {
        core::Logger::instance().warn("EventBus: emitSync timed out for '{}'", eventName);
    }

    return lastResult;
}

void EventBus::releaseAllListeners() {
    std::map<int, ListenerEntry> listeners;
    {
        std::lock_guard lock(_listenersMutex);
        listeners = std::move(_listeners);
        _listenersByName.clear();
    }

    /* Free all JSValues. This is called ON the script thread. */
    for (auto& [handle, entry] : listeners) {
        JSValue val = JS_MKPTR(JS_TAG_OBJECT, entry.callbackPtr);
        JS_FreeValue(entry.ctx, val);
    }
}

} // namespace noix::runtime
