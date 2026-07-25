#pragma once

#include "debug/CdpDispatcher.h"
#include "debug/WebSocketServer.h"
#include "debug/JsDebugBridge.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

namespace noix::debug {

/// CDP session: receives WS messages, dispatches to CdpDispatcher,
/// polls events from JsDebugBridge, sends CDP responses/events.
class CdpSession {
public:
    CdpSession(WebSocketServer& server, JsDebugBridge& bridge);
    ~CdpSession();

    void start();
    void stop();

private:
    void onMessage(const std::string& message);
    void processMessageLoop();
    void sendResponse(int id, cJSON* result);
    void sendError(int id, int code, const std::string& message);
    void sendEvent(const std::string& method, cJSON* params);
    void flushPendingEvents();
    void eventPollLoop();

    WebSocketServer& _server;
    JsDebugBridge& _bridge;
    CdpDispatcher _dispatcher;
    std::thread _pollThread;
    std::thread _dispatchThread;
    std::atomic<bool> _running{false};

    // Message queue: WS thread -> dispatch thread
    std::mutex _msgMutex;
    std::condition_variable _msgCv;
    std::queue<std::string> _msgQueue;
};

} // namespace noix::debug
