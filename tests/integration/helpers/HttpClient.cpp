#include "HttpClient.h"
#include <SDL3_net/SDL_net.h>
#include <chrono>
#include <thread>

namespace noix::test {

HttpClient::HttpClient(const std::string& host, uint16_t port, int timeoutMs)
    : _host(host), _port(port), _timeoutMs(timeoutMs) {}

HttpClient::~HttpClient() {
    disconnect();
}

bool HttpClient::connect() {
    if (_socket) return true;

    if (!NET_Init()) {
        return false;
    }
    _netInitialized = true;

    NET_Address* addr = NET_ResolveHostname(_host.c_str());
    if (!addr) {
        disconnect();
        return false;
    }

    if (NET_WaitUntilResolved(addr, _timeoutMs) != NET_SUCCESS) {
        NET_UnrefAddress(addr);
        disconnect();
        return false;
    }

    _socket = NET_CreateClient(addr, _port, 0);
    NET_UnrefAddress(addr);

    if (!_socket) {
        disconnect();
        return false;
    }

    if (NET_WaitUntilConnected(_socket, _timeoutMs) != NET_SUCCESS) {
        disconnect();
        return false;
    }

    return true;
}

void HttpClient::disconnect() {
    if (_socket) {
        NET_DestroyStreamSocket(_socket);
        _socket = nullptr;
    }
    if (_netInitialized) {
        NET_Quit();
        _netInitialized = false;
    }
}

HttpResponse HttpClient::get(const std::string& path) {
    return sendRequest("GET", path, "");
}

HttpResponse HttpClient::post(const std::string& path, const std::string& body) {
    return sendRequest("POST", path, body);
}

HttpResponse HttpClient::sendRequest(const std::string& method, const std::string& path,
                                      const std::string& body) {
    HttpResponse response;

    if (!connect()) return response;

    // Build HTTP request
    std::string request = method + " " + path + " HTTP/1.1\r\n"
        "Host: " + _host + "\r\n"
        "Connection: close\r\n";

    if (!body.empty()) {
        request += "Content-Type: application/json\r\n"
                   "Content-Length: " + std::to_string(body.size()) + "\r\n"
                   "\r\n" + body;
    } else {
        request += "\r\n";
    }

    // Send
    if (!NET_WriteToStreamSocket(_socket, request.data(),
                                  static_cast<int>(request.size()))) {
        disconnect();
        return response;
    }
    NET_WaitUntilStreamSocketDrained(_socket, _timeoutMs);

    // Read response
    std::string recvBuffer;
    char buf[4096];
    auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(_timeoutMs);

    while (std::chrono::steady_clock::now() < deadline) {
        int n = NET_ReadFromStreamSocket(_socket, buf, sizeof(buf));
        if (n < 0) break;
        if (n > 0) recvBuffer.append(buf, n);

        size_t headerEnd = recvBuffer.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            size_t bodyOffset = headerEnd + 4;
            size_t contentLength = 0;
            size_t pos = 0;
            while (pos < headerEnd) {
                size_t lineEnd = recvBuffer.find("\r\n", pos);
                if (lineEnd == std::string::npos || lineEnd > headerEnd) break;
                std::string headerLine = recvBuffer.substr(pos, lineEnd - pos);
                if (headerLine.size() > 16 &&
                    SDL_strncasecmp(headerLine.c_str(), "Content-Length:", 15) == 0) {
                    contentLength = static_cast<size_t>(std::stoul(headerLine.substr(16)));
                }
                pos = lineEnd + 2;
            }
            if (recvBuffer.size() >= bodyOffset + contentLength) {
                size_t lineEnd = recvBuffer.find("\r\n");
                std::string statusLine = recvBuffer.substr(0, lineEnd);
                size_t firstSpace = statusLine.find(' ');
                if (firstSpace != std::string::npos) {
                    size_t secondSpace = statusLine.find(' ', firstSpace + 1);
                    std::string codeStr = (secondSpace != std::string::npos)
                        ? statusLine.substr(firstSpace + 1, secondSpace - firstSpace - 1)
                        : statusLine.substr(firstSpace + 1);
                    response.statusCode = std::stoi(codeStr);
                }
                response.body = recvBuffer.substr(bodyOffset, contentLength);
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Disconnect after each request (Connection: close)
    disconnect();
    return response;
}

} // namespace noix::test
