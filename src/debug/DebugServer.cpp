#include "debug/DebugServer.h"
#include "core/Logger.h"
#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>
#include <cJSON.h>
#include <chrono>

namespace noix::debug {

DebugServer::DebugServer(uint16_t port, uint32_t timeoutSeconds)
    : _port(port)
    , _startTimeMs(static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now().time_since_epoch()).count()))
{
}

DebugServer::~DebugServer() {
    stop();
}

void DebugServer::start() {
    if (_running.load()) return;

    if (!NET_Init()) {
        core::Logger::instance().error("DebugServer: NET_Init failed: {}", SDL_GetError());
        return;
    }

    _server = NET_CreateServer(nullptr, _port, 0);
    if (!_server) {
        core::Logger::instance().error("DebugServer: NET_CreateServer failed on port {}: {}", _port, SDL_GetError());
        NET_Quit();
        return;
    }

    _running = true;
    _thread = std::thread(&DebugServer::serverLoop, this);
    core::Logger::instance().info("DebugServer (shell) listening on port {}", _port);
}

void DebugServer::stop() {
    if (!_running.load()) return;
    _running = false;

    if (_server) {
        NET_DestroyServer(_server);
        _server = nullptr;
    }
    if (_thread.joinable()) {
        _thread.join();
    }
    NET_Quit();
}

void DebugServer::serverLoop() {
    while (_running.load()) {
        NET_StreamSocket* client = nullptr;
        NET_AcceptClient(_server, &client);

        if (!client) {
            SDL_Delay(10);
            continue;
        }

        /* Read request with timeout */
        std::string recvBuf;
        std::string method, path, body;

        bool gotRequest = false;
        auto deadline = SDL_GetTicks() + 5000;

        while (SDL_GetTicks() < deadline) {
            if (!NET_WaitUntilInputAvailable(reinterpret_cast<void**>(&client), 1, 200)) continue;
            char buf[4096];
            int n = NET_ReadFromStreamSocket(client, buf, sizeof(buf));
            if (n <= 0) break;
            recvBuf.append(buf, n);

            /* Parse HTTP request */
            auto headerEnd = recvBuf.find("\r\n\r\n");
            if (headerEnd == std::string::npos) continue;

            /* Parse request line */
            auto firstSpace = recvBuf.find(' ');
            if (firstSpace == std::string::npos) break;
            method = recvBuf.substr(0, firstSpace);

            auto secondSpace = recvBuf.find(' ', firstSpace + 1);
            if (secondSpace == std::string::npos) break;
            path = recvBuf.substr(firstSpace + 1, secondSpace - firstSpace - 1);

            body = recvBuf.substr(headerEnd + 4);
            gotRequest = true;
            break;
        }

        if (gotRequest) {
            std::string response = handleRequest(method, path, body);
            sendResponse(client, 200, "OK", response);
        } else {
            sendResponse(client, 400, "Bad Request", "{}");
        }

        NET_WaitUntilStreamSocketDrained(client, 1000);
        NET_DestroyStreamSocket(client);
    }
}

std::string DebugServer::handleRequest(const std::string& method, const std::string& path,
                                         const std::string& body) {
    if (path == "/debug/ping") {
        return R"({"status":"ok"})";
    }

    if (path == "/debug/handshake") {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "name", "noix-engine");
        cJSON_AddStringToObject(root, "version", "0.1.0");
        cJSON_AddStringToObject(root, "protocol", "http-shell");
        char* json = cJSON_PrintUnformatted(root);
        std::string result(json);
        cJSON_free(json);
        cJSON_Delete(root);
        return result;
    }

    if (path == "/debug/status") {
        uint64_t now = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        cJSON* root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "uptimeMs", static_cast<double>(now - _startTimeMs));
        cJSON_AddStringToObject(root, "status", "running");
        cJSON_AddStringToObject(root, "note", "DAP debug available via --dap-port");
        char* json = cJSON_PrintUnformatted(root);
        std::string result(json);
        cJSON_free(json);
        cJSON_Delete(root);
        return result;
    }

    return R"({"error":"not found","hint":"Use DAP protocol (--dap-port) for debugging"})";
}

void DebugServer::sendResponse(void* socket, int statusCode, const std::string& statusText,
                                 const std::string& body) {
    auto* sock = static_cast<NET_StreamSocket*>(socket);
    std::string header = "HTTP/1.1 " + std::to_string(statusCode) + " " + statusText + "\r\n";
    header += "Content-Type: application/json\r\n";
    header += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    header += "Access-Control-Allow-Origin: *\r\n";
    header += "\r\n";
    std::string response = header + body;
    NET_WriteToStreamSocket(sock, response.data(), static_cast<int>(response.size()));
}

} // namespace noix::debug
