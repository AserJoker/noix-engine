#pragma once

/*
 * HttpServer — lightweight HTTP server with external Command registration.
 * All endpoints use POST with JSON request/response bodies.
 * Route: POST /api/v1/{endpoint} → findApi(endpoint) → execute(request)
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

class HttpServer {
public:
    HttpServer(uint16_t port);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void start();
    void stop();

    /// Register a command. Must be called before start().
    void addApi(std::shared_ptr<Command> cmd);

    /// Query registered command names (for SchemaCommand)
    std::vector<std::string> apiNames() const;

    /// Find a command by name, returns nullptr if not found
    Command* findApi(const std::string& name);
    const Command* findApi(const std::string& name) const;

private:
    void serverLoop();
    HandlerResult handleRequest(const std::string& path, const std::string& body);

    uint16_t _port;
    NET_Server* _server = nullptr;
    std::thread _thread;
    std::atomic<bool> _running{false};

    std::map<std::string, std::shared_ptr<Command>> _commands;
    mutable std::mutex _commandsMutex;
};

} // namespace noix::debug
