#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct NET_Server;
struct NET_StreamSocket;

namespace noix::debug {

/// WebSocket server built on SDL3_net. Single-client model.
/// Supports HTTP upgrade handshake, text frames, and /json discovery endpoint.
class WebSocketServer {
public:
    using MessageHandler = std::function<void(const std::string&)>;

    explicit WebSocketServer(uint16_t port = 9222);
    ~WebSocketServer();

    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    bool start();
    void stop();

    void send(const std::string& message);
    void setMessageHandler(MessageHandler handler);

    uint16_t port() const { return _port; }

private:
    struct FrameHeader {
        uint8_t opcode;
        bool fin;
        std::string payload;
    };

    void serverLoop();
    void handleNewConnection(NET_StreamSocket* sock);
    void processClient();
    bool handleHttpGet(NET_StreamSocket* sock, const std::string& request);
    std::optional<FrameHeader> readFrame();
    void sendFrame(uint8_t opcode, const std::string& payload);
    void sendClose(uint16_t code, const std::string& reason);

    uint16_t _port;
    std::string _protocolJson;  // cached /json/protocol response
    NET_Server* _server = nullptr;
    NET_StreamSocket* _client = nullptr;
    std::thread _thread;
    std::atomic<bool> _running{false};
    std::vector<char> _recvBuf;
    MessageHandler _onMessage;
    bool _upgraded = false;
    std::mutex _sendMutex;  // protects sendFrame from concurrent calls
};

} // namespace noix::debug
