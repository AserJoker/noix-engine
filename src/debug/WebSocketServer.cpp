#include "debug/WebSocketServer.h"
#include "debug/Sha1.h"
#include "debug/Base64.h"
#include "core/Logger.h"
#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>

namespace noix::debug {

static const std::string kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

WebSocketServer::WebSocketServer(uint16_t port)
    : _port(port) {
    // Try to load CDP protocol description from cdp_protocol.json
    // (co-located with the executable)
    std::string exePath = SDL_GetBasePath() ? SDL_GetBasePath() : "";
    std::string protoPath = exePath + "cdp_protocol.json";
    std::ifstream pf(protoPath);
    if (pf.is_open()) {
        std::ostringstream ss;
        ss << pf.rdbuf();
        _protocolJson = ss.str();
    }
    if (_protocolJson.empty()) {
        // Minimal fallback — just enough for basic CRI discovery
        _protocolJson = R"({"domains":[]})";
    }
}

WebSocketServer::~WebSocketServer() {
    stop();
}

bool WebSocketServer::start() {
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
    _thread = std::thread(&WebSocketServer::serverLoop, this);

    core::Logger::instance().info("WebSocketServer listening on port {}", _port);
    return true;
}

void WebSocketServer::stop() {
    if (!_running.load()) return;

    _running.store(false);
    if (_thread.joinable()) _thread.join();

    if (_client) {
        NET_DestroyStreamSocket(_client);
        _client = nullptr;
    }
    if (_server) {
        NET_DestroyServer(_server);
        _server = nullptr;
    }

    NET_Quit();
    _upgraded = false;
    _recvBuf.clear();
}

void WebSocketServer::send(const std::string& message) {
    if (!_client || !_upgraded) return;
    std::lock_guard lock(_sendMutex);
    sendFrame(0x1, message);
}

void WebSocketServer::setMessageHandler(MessageHandler handler) {
    _onMessage = std::move(handler);
}

// ---- Server loop (like Node.js inspector) ----
//
// Chrome DevTools connection pattern:
// 1. GET /json — HTTP request, respond with target list, close
// 2. GET /json/version — HTTP request, respond, close
// 3. WebSocket upgrade — upgrade to WS, keep open for CDP messages
//
// Key: /json and WS are on SEPARATE TCP connections.
// We must handle /json quickly without blocking the WS connection.

void WebSocketServer::serverLoop() {
    while (_running.load()) {
        // Accept and immediately classify new connections
        NET_StreamSocket* newSock = nullptr;
        while (NET_AcceptClient(_server, &newSock) && newSock != nullptr) {
            handleNewConnection(newSock);
        }

        // Process data from the WS client
        if (_client) {
            processClient();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void WebSocketServer::handleNewConnection(NET_StreamSocket* sock) {
    // Try to peek at initial data to distinguish HTTP /json from WS upgrade.
    // If no data is available yet, store as client — processClient will handle it.
    std::string initialData;
    char buf[4096];
    int n = NET_ReadFromStreamSocket(sock, buf, sizeof(buf));
    if (n > 0) {
        initialData.append(buf, n);
    }

    if (initialData.empty()) {
        // No data yet — store as client, processClient will handle upgrade
        if (_client) NET_DestroyStreamSocket(_client);
        _client = sock;
        _upgraded = false;
        _recvBuf.clear();
        core::Logger::instance().info("Client accepted (data pending)");
        return;
    }

    // HTTP /json request — respond and close immediately
    if (initialData.find("GET /json") != std::string::npos &&
        initialData.find("Upgrade:") == std::string::npos &&
        initialData.find("upgrade:") == std::string::npos) {
        handleHttpGet(sock, initialData);
        NET_DestroyStreamSocket(sock);
        return;
    }

    // WebSocket upgrade — store as client with buffered data
    if (_client) NET_DestroyStreamSocket(_client);
    _client = sock;
    _upgraded = false;
    _recvBuf.assign(initialData.begin(), initialData.end());
    core::Logger::instance().info("WS client accepted (with upgrade data)");
}

void WebSocketServer::processClient() {
    if (!_client) return;

    char buf[4096];
    int n = NET_ReadFromStreamSocket(_client, buf, sizeof(buf));

    if (n < 0) {
        if (_recvBuf.empty()) {
            core::Logger::instance().info("WS client disconnected (n={})", n);
            NET_DestroyStreamSocket(_client);
            _client = nullptr;
            _upgraded = false;
            return;
        }
    }

    if (n > 0) {
        _recvBuf.insert(_recvBuf.end(), buf, buf + n);
    }

    if (_recvBuf.empty()) return;

    // Handle WS upgrade if not yet upgraded
    if (!_upgraded) {
        std::string data(_recvBuf.begin(), _recvBuf.end());
        core::Logger::instance().debug("UPGRADE CHECK: buf size={}", data.size());
        size_t headerEnd = data.find("\r\n\r\n");
        if (headerEnd == std::string::npos) return;

        std::string request = data.substr(0, headerEnd + 4);
        _recvBuf.erase(_recvBuf.begin(),
                       _recvBuf.begin() + static_cast<ptrdiff_t>(headerEnd + 4));

        // Case-insensitive check for "Upgrade: websocket"
        std::string lowerRequest = request;
        std::transform(lowerRequest.begin(), lowerRequest.end(), lowerRequest.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (lowerRequest.find("upgrade: websocket") == std::string::npos) {
            // Check if this is an HTTP request (e.g. /json) that arrived late
            if (lowerRequest.find("get /json") != std::string::npos) {
                handleHttpGet(_client, request);
                NET_DestroyStreamSocket(_client);
                _client = nullptr;
                return;
            }
            core::Logger::instance().info("Non-WS request, closing");
            NET_DestroyStreamSocket(_client);
            _client = nullptr;
            return;
        }

        // Extract Sec-WebSocket-Key (case-insensitive search)
        size_t keyPos = lowerRequest.find("sec-websocket-key:");
        if (keyPos == std::string::npos) {
            NET_DestroyStreamSocket(_client);
            _client = nullptr;
            return;
        }

        size_t valueStart = request.find(':', keyPos) + 1;
        size_t lineEnd = request.find("\r\n", valueStart);
        std::string wsKey = request.substr(valueStart, lineEnd - valueStart);
        while (!wsKey.empty() && (wsKey.front() == ' ' || wsKey.front() == '\t'))
            wsKey.erase(wsKey.begin());
        while (!wsKey.empty() && (wsKey.back() == ' ' || wsKey.back() == '\t' ||
               wsKey.back() == '\r' || wsKey.back() == '\n'))
            wsKey.pop_back();

        // Compute accept key
        auto hash = sha1(wsKey + kWsGuid);
        std::string acceptKey = base64Encode(hash);

        std::string response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + acceptKey + "\r\n"
            "\r\n";

        NET_WriteToStreamSocket(_client, response.data(),
                                static_cast<int>(response.size()));
        _upgraded = true;
        core::Logger::instance().info("WS handshake complete");
    }

    // Read WS frames
    while (auto frame = readFrame()) {
        switch (frame->opcode) {
        case 0x1: // text
            core::Logger::instance().debug("WS MSG: {}", frame->payload);
            if (_onMessage) _onMessage(frame->payload);
            break;
        case 0x8: // close
            sendClose(1000, "bye");
            NET_DestroyStreamSocket(_client);
            _client = nullptr;
            _upgraded = false;
            _recvBuf.clear();
            return;
        case 0x9: // ping
            sendFrame(0xA, frame->payload);
            break;
        default:
            break;
        }
    }
}

bool WebSocketServer::handleHttpGet(NET_StreamSocket* sock, const std::string& request) {
    std::string json;
    if (request.find("/json/version") != std::string::npos) {
        json = R"({
  "Browser": "noix-engine/1.0",
  "Protocol-Version": "1.3",
  "User-Agent": "noix-engine",
  "V8-Version": "quickjs-ng",
  "WebKit-Version": "0"
})";
    } else if (request.find("/json/protocol") != std::string::npos) {
        json = _protocolJson;
    } else {
        json = R"([{
  "description": "noix-engine CDP debug bridge",
  "devtoolsFrontendUrl": "devtools://devtools/bundled/inspector.html?ws=localhost:)" +
            std::to_string(_port) + R"(",
  "id": "0",
  "title": "noix-engine",
  "type": "node",
  "url": "file://)" +
            std::to_string(_port) + R"(",
  "webSocketDebuggerUrl": "ws://localhost:)" +
            std::to_string(_port) + R"("
}])";
    }

    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(json.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + json;

    NET_WriteToStreamSocket(sock, response.data(), static_cast<int>(response.size()));
    NET_WaitUntilStreamSocketDrained(sock, 500);
    return true;
}

std::optional<WebSocketServer::FrameHeader> WebSocketServer::readFrame() {
    if (_recvBuf.size() < 2) return std::nullopt;

    uint8_t b0 = static_cast<uint8_t>(_recvBuf[0]);
    uint8_t b1 = static_cast<uint8_t>(_recvBuf[1]);

    bool fin = (b0 & 0x80) != 0;
    uint8_t opcode = b0 & 0x0F;
    bool masked = (b1 & 0x80) != 0;
    uint64_t payloadLen = b1 & 0x7F;

    size_t headerSize = 2;

    if (payloadLen == 126) {
        if (_recvBuf.size() < 4) return std::nullopt;
        payloadLen = (static_cast<uint64_t>(static_cast<uint8_t>(_recvBuf[2])) << 8) |
                      static_cast<uint8_t>(_recvBuf[3]);
        headerSize = 4;
    } else if (payloadLen == 127) {
        if (_recvBuf.size() < 10) return std::nullopt;
        payloadLen = 0;
        for (int i = 0; i < 8; ++i) {
            payloadLen = (payloadLen << 8) | static_cast<uint8_t>(_recvBuf[2 + i]);
        }
        headerSize = 10;
    }

    if (masked) headerSize += 4;

    if (_recvBuf.size() < headerSize + payloadLen) return std::nullopt;

    uint8_t mask[4] = {0};
    if (masked) {
        for (int i = 0; i < 4; ++i) {
            mask[i] = static_cast<uint8_t>(_recvBuf[headerSize - 4 + i]);
        }
    }

    std::string payload;
    payload.resize(static_cast<size_t>(payloadLen));
    for (uint64_t i = 0; i < payloadLen; ++i) {
        payload[static_cast<size_t>(i)] =
            _recvBuf[headerSize + static_cast<size_t>(i)] ^ mask[i % 4];
    }

    _recvBuf.erase(_recvBuf.begin(),
                   _recvBuf.begin() + static_cast<ptrdiff_t>(headerSize + payloadLen));

    return FrameHeader{opcode, fin, std::move(payload)};
}

void WebSocketServer::sendFrame(uint8_t opcode, const std::string& payload) {
    if (!_client) return;

    std::vector<uint8_t> frame;
    frame.push_back(0x80 | opcode);

    size_t len = payload.size();
    if (len <= 125) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }

    frame.insert(frame.end(), payload.begin(), payload.end());
    NET_WriteToStreamSocket(_client, reinterpret_cast<char*>(frame.data()),
                            static_cast<int>(frame.size()));
}

void WebSocketServer::sendClose(uint16_t code, const std::string& reason) {
    std::string payload;
    payload.push_back(static_cast<uint8_t>((code >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(code & 0xFF));
    payload += reason;
    sendFrame(0x8, payload);
}

} // namespace noix::debug
