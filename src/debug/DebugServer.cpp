#include "debug/DebugServer.h"
#include "core/Logger.h"
#include "core/Value.h"

#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>

namespace noix::debug {

DebugServer::DebugServer(uint16_t port, const std::string& apiBase)
    : _port(port), _apiBase(apiBase) {}

DebugServer::~DebugServer() {
    stop();
}

void DebugServer::addApi(std::shared_ptr<Command> cmd) {
    std::lock_guard<std::mutex> lk(_commandsMutex);
    std::string key = cmd->version() + "/" + cmd->name();
    _commands[key] = std::move(cmd);
}

std::map<std::string, std::vector<std::string>> DebugServer::apiNamesByVersion() const {
    std::lock_guard<std::mutex> lk(_commandsMutex);
    std::map<std::string, std::vector<std::string>> result;
    for (auto& [key, cmd] : _commands) {
        result[cmd->version()].push_back(cmd->name());
    }
    return result;
}

Command* DebugServer::findApi(const std::string& version, const std::string& name) {
    std::lock_guard<std::mutex> lk(_commandsMutex);
    auto it = _commands.find(version + "/" + name);
    return it != _commands.end() ? it->second.get() : nullptr;
}

const Command* DebugServer::findApi(const std::string& version, const std::string& name) const {
    std::lock_guard<std::mutex> lk(_commandsMutex);
    auto it = _commands.find(version + "/" + name);
    return it != _commands.end() ? it->second.get() : nullptr;
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
    core::Logger::instance().info("DebugServer listening on port {}", _port);
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

        /* Read HTTP request with timeout */
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

            auto headerEnd = recvBuf.find("\r\n\r\n");
            if (headerEnd == std::string::npos) continue;

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

        HandlerResult result;
        if (gotRequest && method == "POST") {
            result = handleRequest(path, body);
        } else if (gotRequest) {
            result = {405, R"({"error": "method not allowed"})"_json.dump()};
        } else {
            result = {400, R"({"error": "bad request"})"_json.dump()};
        }

        /* Send response */
        auto statusText = (result.statusCode == 200) ? "OK" :
                          (result.statusCode == 202) ? "Accepted" :
                          (result.statusCode == 404) ? "Not Found" :
                          (result.statusCode == 405) ? "Method Not Allowed" : "OK";
        std::string header = "HTTP/1.1 " + std::to_string(result.statusCode) + " " + statusText + "\r\n";
        header += "Content-Type: application/json\r\n";
        header += "Content-Length: " + std::to_string(result.body.size()) + "\r\n";
        header += "Access-Control-Allow-Origin: *\r\n";
        header += "\r\n";
        std::string response = header + result.body;
        NET_WriteToStreamSocket(client, response.data(), static_cast<int>(response.size()));

        NET_WaitUntilStreamSocketDrained(client, 1000);
        NET_DestroyStreamSocket(client);
    }

    core::Logger::instance().info("DebugServer: server thread exited");
}

HandlerResult DebugServer::handleRequest(const std::string& path, const std::string& body) {
    /* Extract version and endpoint from path: /{apiBase}/{version}/{endpoint}
       e.g. "/api/v1/system/ping" → version="v1", endpoint="system/ping" */
    if (path.size() <= _apiBase.size() || path.substr(0, _apiBase.size()) != _apiBase || path[_apiBase.size()] != '/') {
        return {404, Value::object({{"error", Value("not found")}, {"path", Value(path)}}).dump()};
    }

    std::string rest = path.substr(_apiBase.size() + 1); /* "v1/system/ping" */
    auto slashPos = rest.find('/');
    if (slashPos == std::string::npos) {
        return {404, Value::object({{"error", Value("not found")}, {"path", Value(path)}}).dump()};
    }

    std::string version = rest.substr(0, slashPos);   /* "v1" */
    std::string endpoint = rest.substr(slashPos + 1);  /* "system/ping" */

    Command* cmd = findApi(version, endpoint);
    if (!cmd) {
        return {404, Value::object({{"error", Value("not found")}, {"path", Value(path)}}).dump()};
    }

    /* Parse request body as JSON */
    Value request = body.empty() ? Value::object() : Value::parse(body);
    if (request.isNull()) {
        request = Value::object();
    }

    Value response = cmd->execute(request);
    return {200, response.dump()};
}

} // namespace noix::debug
