#include "debug/HttpServer.h"
#include "core/Logger.h"
#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>
#include <chrono>

namespace noix::debug {

static constexpr size_t kMaxRecvBufferSize = 1024 * 1024; // 1MB

HttpServer::HttpServer(uint16_t port)
    : _port(port) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::get(const std::string& path, Handler handler) {
    std::lock_guard lock(_routesMutex);
    _routes.push_back({"GET", path, std::move(handler)});
}

void HttpServer::post(const std::string& path, Handler handler) {
    std::lock_guard lock(_routesMutex);
    _routes.push_back({"POST", path, std::move(handler)});
}

bool HttpServer::start() {
    if (_running.load()) return true;

    if (!NET_Init()) {
        core::Logger::instance().error("NET_Init failed: {}", SDL_GetError());
        return false;
    }

    _server = NET_CreateServer(nullptr, _port, 0);
    if (!_server) {
        core::Logger::instance().error("NET_CreateServer failed: {}", SDL_GetError());
        NET_Quit();
        return false;
    }

    _running.store(true);
    _thread = std::thread(&HttpServer::serverLoop, this);

    core::Logger::instance().info("HttpServer listening on port {}", _port);
    return true;
}

void HttpServer::stop() {
    if (!_running.load()) return;

    _running.store(false);
    if (_thread.joinable()) {
        _thread.join();
    }

    for (auto& conn : _connections) {
        NET_DestroyStreamSocket(conn.socket);
    }
    _connections.clear();

    if (_server) {
        NET_DestroyServer(_server);
        _server = nullptr;
    }

    NET_Quit();
}

void HttpServer::serverLoop() {
    while (_running.load()) {
        acceptConnections();
        processConnections();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    for (auto& conn : _connections) {
        NET_DestroyStreamSocket(conn.socket);
    }
    _connections.clear();
}

void HttpServer::acceptConnections() {
    NET_StreamSocket* client = nullptr;
    while (NET_AcceptClient(_server, &client) && client != nullptr) {
        _connections.push_back(Connection{client, {}});
    }
}

void HttpServer::processConnections() {
    for (auto it = _connections.begin(); it != _connections.end();) {
        if (processOneConnection(*it)) {
            NET_DestroyStreamSocket(it->socket);
            it = _connections.erase(it);
        } else {
            ++it;
        }
    }
}

bool HttpServer::processOneConnection(Connection& conn) {
    char buf[4096];
    int n = NET_ReadFromStreamSocket(conn.socket, buf, sizeof(buf));

    if (n < 0) return true;  // error, close
    if (n == 0) return false; // no data yet

    conn.recvBuffer.append(buf, n);

    if (conn.recvBuffer.size() > kMaxRecvBufferSize) return true; // overflow

    std::string method, path, body;
    if (!parseRequest(conn.recvBuffer, method, path, body)) return false;

    std::string responseBody;
    bool matched = false;

    {
        std::lock_guard lock(_routesMutex);
        for (const auto& route : _routes) {
            if (route.method == method && route.path == path) {
                responseBody = route.handler(body);
                matched = true;
                break;
            }
        }
    }

    if (matched) {
        sendResponse(conn.socket, 200, "OK", responseBody);
    } else {
        sendResponse(conn.socket, 404, "Not Found", R"({"error":"not found"})");
    }

    return true;
}

bool HttpServer::parseRequest(std::string& recvBuffer, std::string& method,
                               std::string& path, std::string& body) {
    size_t headerEnd = recvBuffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return false;

    size_t bodyOffset = headerEnd + 4;

    // Parse request line
    size_t lineEnd = recvBuffer.find("\r\n");
    if (lineEnd == std::string::npos || lineEnd >= headerEnd) return false;

    const std::string requestLine = recvBuffer.substr(0, lineEnd);
    size_t firstSpace = requestLine.find(' ');
    size_t secondSpace = requestLine.find(' ', firstSpace + 1);
    if (firstSpace == std::string::npos || secondSpace == std::string::npos) return false;

    method = requestLine.substr(0, firstSpace);
    path = requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);

    // Parse Content-Length
    size_t contentLength = 0;
    size_t pos = lineEnd + 2;
    while (pos < headerEnd) {
        size_t nextLine = recvBuffer.find("\r\n", pos);
        if (nextLine == std::string::npos || nextLine > headerEnd) break;

        std::string headerLine = recvBuffer.substr(pos, nextLine - pos);
        if (headerLine.size() > 16 &&
            SDL_strncasecmp(headerLine.c_str(), "Content-Length:", 15) == 0) {
            contentLength = static_cast<size_t>(std::stoul(headerLine.substr(16)));
        }
        pos = nextLine + 2;
    }

    if (recvBuffer.size() < bodyOffset + contentLength) return false;

    body = recvBuffer.substr(bodyOffset, contentLength);
    recvBuffer.erase(0, bodyOffset + contentLength);
    return true;
}

void HttpServer::sendResponse(NET_StreamSocket* sock, int statusCode,
                               const std::string& statusText,
                               const std::string& body) {
    std::string response =
        "HTTP/1.1 " + std::to_string(statusCode) + " " + statusText + "\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        body;

    NET_WriteToStreamSocket(sock, response.data(), static_cast<int>(response.size()));
    NET_WaitUntilStreamSocketDrained(sock, 500);
}

} // namespace noix::debug
