#pragma once

#include <cstdint>
#include <string>

struct NET_StreamSocket;

namespace noix::test {

struct HttpResponse {
    int statusCode = 0;
    std::string body;
};

class HttpClient {
public:
    HttpClient(const std::string& host, uint16_t port, int timeoutMs = 5000);
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    HttpResponse get(const std::string& path);
    HttpResponse post(const std::string& path, const std::string& body);

private:
    HttpResponse sendRequest(const std::string& method, const std::string& path,
                             const std::string& body);
    bool connect();
    void disconnect();

    std::string _host;
    uint16_t _port;
    int _timeoutMs;
    NET_StreamSocket* _socket = nullptr;
    bool _netInitialized = false;
};

} // namespace noix::test
