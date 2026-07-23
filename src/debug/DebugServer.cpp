#include "debug/DebugServer.h"
#include "core/Logger.h"
#include <SDL3/SDL.h>
#include <cJSON.h>
#include <chrono>
#include <random>

namespace noix::debug {

static uint64_t nowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

static std::string generateId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    char buf[17];
    snprintf(buf, sizeof(buf), "%08x%08x", dist(gen), dist(gen));
    return buf;
}

static std::string jsonSuccess(const std::string& ns, const std::string& command, const std::string& result) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "command", command.c_str());
    cJSON_AddStringToObject(root, "namespace", ns.c_str());
    cJSON_AddStringToObject(root, "result", result.c_str());
    char* json = cJSON_PrintUnformatted(root);
    std::string str(json);
    cJSON_Delete(root);
    cJSON_free(json);
    return str;
}

static std::string jsonSuccessObj(const std::string& ns, const std::string& command, cJSON* resultObj) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "command", command.c_str());
    cJSON_AddStringToObject(root, "namespace", ns.c_str());
    cJSON_AddItemToObject(root, "result", resultObj);
    char* json = cJSON_PrintUnformatted(root);
    std::string str(json);
    cJSON_Delete(root);
    cJSON_free(json);
    return str;
}

static std::string jsonError(const std::string& error, const std::string& command = "") {
    cJSON* root = cJSON_CreateObject();
    if (!command.empty()) {
        cJSON_AddStringToObject(root, "command", command.c_str());
    }
    cJSON_AddStringToObject(root, "error", error.c_str());
    char* json = cJSON_PrintUnformatted(root);
    std::string str(json);
    cJSON_Delete(root);
    cJSON_free(json);
    return str;
}

DebugServer::DebugServer(uint16_t port, uint32_t timeoutSeconds)
    : _httpServer(port), _timeoutSeconds(timeoutSeconds) {
    registerRoutes();
}

DebugServer::~DebugServer() {
    stop();
}

void DebugServer::registerCommand(const core::NamespacedId& id, CommandPtr cmd) {
    std::lock_guard lock(_commandMutex);
    _commands[id] = std::move(cmd);
}

void DebugServer::unregisterCommand(const core::NamespacedId& id) {
    std::lock_guard lock(_commandMutex);
    _commands.erase(id);
}

std::string DebugServer::createSession(const std::string& clientName, const std::string& clientVersion) {
    std::lock_guard lock(_sessionMutex);
    Session s;
    s.id = generateId();
    s.clientName = clientName;
    s.clientVersion = clientVersion;
    s.lastRequest = nowMs();
    _sessions[s.id] = s;
    core::Logger::instance().info("session created: {} ({})", s.id, clientName);
    return s.id;
}

bool DebugServer::refreshSession(const std::string& id) {
    std::lock_guard lock(_sessionMutex);
    auto it = _sessions.find(id);
    if (it == _sessions.end()) {
        return false;
    }
    it->second.lastRequest = nowMs();
    return true;
}

void DebugServer::removeSession(const std::string& id) {
    std::lock_guard lock(_sessionMutex);
    auto it = _sessions.find(id);
    if (it != _sessions.end()) {
        core::Logger::instance().info("session removed: {} ({})", id, it->second.clientName);
        _sessions.erase(it);
    }
}

void DebugServer::sessionLoop() {
    while (_running.load()) {
        uint64_t threshold = nowMs() - static_cast<uint64_t>(_timeoutSeconds) * 1000;

        {
            std::lock_guard lock(_sessionMutex);
            for (auto it = _sessions.begin(); it != _sessions.end();) {
                if (it->second.lastRequest < threshold) {
                    core::Logger::instance().info("session expired: {} ({})",
                        it->second.id, it->second.clientName);
                    it = _sessions.erase(it);
                } else {
                    ++it;
                }
            }
        }

        std::unique_lock lock(_sessionMutex);
        _sessionCv.wait_for(lock, std::chrono::seconds(5),
            [this] { return !_running.load(); });
    }
}

void DebugServer::registerRoutes() {
    // ---- Handshake ----
    _httpServer.get("/debug/handshake", [](const std::string&) -> std::string {
        cJSON* info = cJSON_CreateObject();
        cJSON_AddStringToObject(info, "name", "noix-engine");
        cJSON_AddStringToObject(info, "version", "0.1.0");
        cJSON_AddStringToObject(info, "protocol", "1.0");
        return jsonSuccessObj("debug", "handshake", info);
    });

    // ---- Simple test route ----
    _httpServer.get("/debug/ping", [](const std::string&) -> std::string {
        return R"({"pong":true})";
    });

    // ---- Initialize: create session ----
    _httpServer.post("/debug/initialize", [this](const std::string& body) -> std::string {
        cJSON* req = cJSON_Parse(body.c_str());
        if (!req) {
            return jsonError("invalid json", "initialize");
        }

        std::string clientName = "unknown";
        std::string clientVersion = "unknown";

        cJSON* args = cJSON_GetObjectItem(req, "arguments");
        if (args && cJSON_IsObject(args)) {
            cJSON* name = cJSON_GetObjectItem(args, "clientName");
            cJSON* ver = cJSON_GetObjectItem(args, "clientVersion");
            if (name && cJSON_IsString(name)) clientName = name->valuestring;
            if (ver && cJSON_IsString(ver)) clientVersion = ver->valuestring;
        }

        cJSON_Delete(req);

        std::string sessionId = createSession(clientName, clientVersion);

        // Collect registered command names for capability reporting
        std::vector<std::string> commands;
        {
            std::lock_guard lock(_commandMutex);
            for (const auto& [key, _] : _commands) {
                commands.push_back(key.toString());
            }
        }

        cJSON* result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "sessionId", sessionId.c_str());
        cJSON_AddNumberToObject(result, "timeoutSeconds", _timeoutSeconds);

        cJSON* cmds = cJSON_CreateArray();
        for (const auto& name : commands) {
            cJSON_AddItemToArray(cmds, cJSON_CreateString(name.c_str()));
        }
        cJSON_AddItemToObject(result, "commands", cmds);
        return jsonSuccessObj("debug", "initialize", result);
    });

    // ---- Disconnect: client explicitly leaves ----
    _httpServer.post("/debug/disconnect", [this](const std::string& body) -> std::string {
        cJSON* req = cJSON_Parse(body.c_str());
        if (!req) {
            return jsonError("invalid json", "disconnect");
        }

        cJSON* args = cJSON_GetObjectItem(req, "arguments");
        if (!args || !cJSON_IsObject(args)) {
            cJSON_Delete(req);
            return jsonError("missing 'arguments' field", "disconnect");
        }

        cJSON* sid = cJSON_GetObjectItem(args, "sessionId");
        if (!sid || !cJSON_IsString(sid)) {
            cJSON_Delete(req);
            return jsonError("missing 'arguments.sessionId' field", "disconnect");
        }

        removeSession(sid->valuestring);
        cJSON_Delete(req);
        return jsonSuccess("debug", "disconnect", "ok");
    });

    // ---- Status ----
    _httpServer.get("/debug/status", [this](const std::string&) -> std::string {
        std::lock_guard lock(_sessionMutex);
        cJSON* result = cJSON_CreateObject();
        cJSON_AddNumberToObject(result, "uptime", SDL_GetTicks() / 1000.0);
        cJSON_AddNumberToObject(result, "sessions", static_cast<double>(_sessions.size()));
        return jsonSuccessObj("debug", "status", result);
    });

    // ---- Command dispatch (all commands require valid session) ----
    _httpServer.post("/debug/command", [this](const std::string& body) -> std::string {
        cJSON* req = cJSON_Parse(body.c_str());
        if (!req) {
            return jsonError("invalid json");
        }

        cJSON* cmd = cJSON_GetObjectItem(req, "command");
        cJSON* ns = cJSON_GetObjectItem(req, "namespace");
        cJSON* args = cJSON_GetObjectItem(req, "arguments");

        if (!cmd || !cJSON_IsString(cmd)) {
            cJSON_Delete(req);
            return jsonError("missing 'command' field");
        }
        if (!ns || !cJSON_IsString(ns)) {
            cJSON_Delete(req);
            return jsonError("missing 'namespace' field");
        }

        std::string command(cmd->valuestring);
        std::string nspace(ns->valuestring);

        // All commands require valid session
        cJSON* sid = args ? cJSON_GetObjectItem(args, "sessionId") : nullptr;
        if (!sid || !cJSON_IsString(sid)) {
            cJSON_Delete(req);
            return jsonError("session expired, please re-initialize", command);
        }

        if (!refreshSession(sid->valuestring)) {
            cJSON_Delete(req);
            return jsonError("session expired, please re-initialize", command);
        }

        // Serialize arguments to string for the handler
        char* argsJson = args ? cJSON_PrintUnformatted(args) : nullptr;
        std::string argsStr = argsJson ? argsJson : "{}";
        cJSON_free(argsJson);

        // Dispatch to registered command
        Command* commandObj = nullptr;
        {
            std::lock_guard lock(_commandMutex);
            auto it = _commands.find(core::NamespacedId(nspace, command));
            if (it != _commands.end()) {
                commandObj = it->second.get();
            }
        }

        cJSON_Delete(req);

        if (commandObj) {
            std::string result = commandObj->execute(argsStr);
            // Try to parse result as JSON and embed as object; fall back to string
            cJSON* resultObj = cJSON_Parse(result.c_str());
            if (resultObj) {
                return jsonSuccessObj(nspace, command, resultObj);
            }
            return jsonSuccess(nspace, command, result);
        }

        return jsonError("unknown command: " + command, command);
    });
}

void DebugServer::start() {
    _running.store(true);
    _sessionThread = std::thread(&DebugServer::sessionLoop, this);
    _httpServer.start();
}

void DebugServer::stop() {
    _running.store(false);
    _sessionCv.notify_one();
    _httpServer.stop();
    if (_sessionThread.joinable()) {
        _sessionThread.join();
    }
}

} // namespace noix::debug
