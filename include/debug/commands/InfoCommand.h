#pragma once

#include "debug/Command.h"
#include "debug/DebugServer.h"
#include "core/Value.h"

namespace noix::debug {

enum class DapTransportMode { None, Tcp, Stdio };

class InfoCommand : public Command {
public:
    InfoCommand(const std::string& version, uint16_t dapPort, DapTransportMode dapTransport,
                const DebugServer& server)
        : _version(version), _dapPort(dapPort), _dapTransport(dapTransport), _server(server) {}

    std::string name() const override { return "system/info"; }
    std::string description() const override { return "Engine identity, API listing, and DAP discovery"; }

    Value requestSchema() const override {
        return Value::object();
    }

    Value responseSchema() const override {
        return R"({
            "type": "object",
            "properties": {
                "name": {"type": "string"},
                "version": {"type": "string"},
                "api": {
                    "type": "object",
                    "properties": {
                        "v1": {"type": "array", "items": {"type": "string"}}
                    }
                },
                "dap": {
                    "type": "object",
                    "properties": {
                        "available": {"type": "boolean"},
                        "transport": {"type": "string", "enum": ["tcp", "stdio", "none"]},
                        "host": {"type": "string"},
                        "port": {"type": "integer"}
                    }
                }
            }
        })"_json;
    }

    Value execute(const Value& /*request*/) override {
        /* Build API listing dynamically from registered commands */
        std::vector<Value> items;
        for (const auto& name : _server.apiNames()) {
            items.emplace_back(name);
        }

        /* Derive version key from prefix: "/api/v1" → "v1" */
        std::string versionKey = _server.apiPrefix();
        auto lastSlash = versionKey.rfind('/');
        if (lastSlash != std::string::npos) {
            versionKey = versionKey.substr(lastSlash + 1);
        }

        Value dapObj;
        if (_dapTransport == DapTransportMode::Tcp) {
            dapObj = R"({
                "available": true,
                "transport": "tcp",
                "host": "127.0.0.1"
            })"_json;
            dapObj.asObject().emplace("port", Value(static_cast<int>(_dapPort)));
        } else if (_dapTransport == DapTransportMode::Stdio) {
            dapObj = R"({
                "available": true,
                "transport": "stdio"
            })"_json;
        } else {
            dapObj = R"({
                "available": false,
                "transport": "none"
            })"_json;
        }

        return Value::object({
            {"name", Value("noix-engine")},
            {"version", Value(_version)},
            {"api", Value::object({{versionKey, Value::array(std::move(items))}})},
            {"dap", dapObj}
        });
    }

private:
    std::string _version;
    uint16_t _dapPort;
    DapTransportMode _dapTransport;
    const DebugServer& _server;
};

} // namespace noix::debug
