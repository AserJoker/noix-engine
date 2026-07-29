#pragma once

/*
 * DebugServer — lightweight HTTP server with external Command registration.
 * All endpoints use POST with JSON request/response bodies.
 * Route: POST /{apiBase}/{version}/{endpoint} → findApi(version, endpoint) → execute(request)
 * Each Command declares its own version via Command::version().
 */

#include "debug/Command.h"

#include <SDL3_net/SDL_net.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace noix::debug {

struct HandlerResult {
    int statusCode;
    std::string body;
};

class DebugServer {
public:
    DebugServer(uint16_t port, const std::string& apiBase = "/api");
    ~DebugServer();

    DebugServer(const DebugServer&) = delete;
    DebugServer& operator=(const DebugServer&) = delete;

    void start();
    void stop();

    /// Register a command. Must be called before start().
    void addApi(std::shared_ptr<Command> cmd);

    /// Query registered command names, grouped by version
    std::map<std::string, std::vector<std::string>> apiNamesByVersion() const;

    /// Find a command by version + name, returns nullptr if not found
    Command* findApi(const std::string& version, const std::string& name);
    const Command* findApi(const std::string& version, const std::string& name) const;

    /// Get the API URL base (e.g. "/api")
    const std::string& apiBase() const { return _apiBase; }

private:
    void serverLoop();
    HandlerResult handleRequest(const std::string& path, const std::string& body);

    uint16_t _port;
    std::string _apiBase;
    NET_Server* _server = nullptr;
    std::thread _thread;
    std::atomic<bool> _running{false};

    /* Key: "version/name", e.g. "v1/system/ping" */
    std::map<std::string, std::shared_ptr<Command>> _commands;
    mutable std::mutex _commandsMutex;
};

} // namespace noix::debug
