#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct NET_Server;
struct NET_StreamSocket;

namespace noix::debug {

class HttpServer {
public:
    using Handler = std::function<std::string(const std::string& body)>;

    HttpServer(uint16_t port = 8080);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void get(const std::string& path, Handler handler);
    void post(const std::string& path, Handler handler);

    bool start();
    void stop();

    uint16_t port() const { return _port; }

private:
    struct Connection {
        NET_StreamSocket* socket;
        std::string recvBuffer;
    };

    struct Route {
        std::string method;
        std::string path;
        Handler handler;
    };

    void serverLoop();
    void acceptConnections();
    void processConnections();
    bool processOneConnection(Connection& conn);
    bool parseRequest(std::string& recvBuffer, std::string& method,
                      std::string& path, std::string& body);
    void sendResponse(NET_StreamSocket* sock, int statusCode,
                      const std::string& statusText, const std::string& body);

    uint16_t _port;
    NET_Server* _server = nullptr;
    std::thread _thread;
    std::atomic<bool> _running{false};
    std::vector<Connection> _connections;
    std::vector<Route> _routes;
    std::mutex _routesMutex;
};

} // namespace noix::debug
