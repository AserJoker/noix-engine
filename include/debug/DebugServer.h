#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

struct NET_Server;

namespace noix::debug {

/**
 * DebugServer — Empty shell HTTP server for backwards compatibility.
 *
 * Provides basic health-check endpoints (ping, handshake, status).
 * All debug functionality has been moved to DapServer (DAP protocol).
 */
class DebugServer {
public:
    DebugServer(uint16_t port = 9900, uint32_t timeoutSeconds = 30);
    ~DebugServer();

    DebugServer(const DebugServer&) = delete;
    DebugServer& operator=(const DebugServer&) = delete;

    void start();
    void stop();

    uint16_t port() const { return _port; }

private:
    void serverLoop();
    std::string handleRequest(const std::string& method, const std::string& path,
                               const std::string& body);
    void sendResponse(void* socket, int statusCode, const std::string& statusText,
                       const std::string& body);

    uint16_t _port;
    NET_Server* _server = nullptr;
    std::thread _thread;
    std::atomic<bool> _running{false};
    uint64_t _startTimeMs;
};

} // namespace noix::debug
