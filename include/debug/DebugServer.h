#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "core/NamespacedId.h"
#include "debug/Command.h"
#include "debug/HttpServer.h"

namespace noix::debug {

struct Session {
    std::string id;
    std::string clientName;
    std::string clientVersion;
    uint64_t lastRequest;
};

class DebugServer {
public:
    using CommandPtr = std::unique_ptr<Command>;

    DebugServer(uint16_t port = 9900, uint32_t timeoutSeconds = 30);
    ~DebugServer();

    DebugServer(const DebugServer&) = delete;
    DebugServer& operator=(const DebugServer&) = delete;

    void registerCommand(const core::NamespacedId& id, CommandPtr cmd);
    void unregisterCommand(const core::NamespacedId& id);
    void start();
    void stop();

    uint16_t port() const { return _httpServer.port(); }

private:
    void registerRoutes();
    void sessionLoop();
    std::string createSession(const std::string& clientName, const std::string& clientVersion);
    bool refreshSession(const std::string& id);
    void removeSession(const std::string& id);

    HttpServer _httpServer;
    uint32_t _timeoutSeconds;
    std::map<std::string, Session> _sessions;
    std::mutex _sessionMutex;
    std::condition_variable _sessionCv;
    std::thread _sessionThread;
    std::atomic<bool> _running{false};

    std::map<core::NamespacedId, CommandPtr> _commands;
    std::mutex _commandMutex;
};

} // namespace noix::debug
