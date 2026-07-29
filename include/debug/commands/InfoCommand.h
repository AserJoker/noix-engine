#pragma once

#include "debug/Command.h"
#include "debug/DebugServer.h"
#include "core/Value.h"

namespace noix::debug {

enum class DapTransportMode { None, Tcp, Stdio };

class InfoCommand : public Command {
public:
    InfoCommand(const std::string& engineVersion, uint16_t dapPort, DapTransportMode dapTransport,
                const DebugServer& server)
        : _engineVersion(engineVersion), _dapPort(dapPort), _dapTransport(dapTransport), _server(server) {}

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
                    "additionalProperties": {
                        "type": "array",
                        "items": {"type": "string"}
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
        /* Build API listing grouped by version from registered commands */
        auto byVersion = _server.apiNamesByVersion();
        std::map<std::string, Value> apiObj;
        for (auto& [ver, names] : byVersion) {
            std::vector<Value> items;
            for (const auto& name : names) {
                items.emplace_back(name);
            }
            apiObj.emplace(ver, Value::array(std::move(items)));
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
            {"version", Value(_engineVersion)},
            {"api", Value::object(std::move(apiObj))},
            {"dap", dapObj}
        });
    }

private:
    std::string _engineVersion;
    uint16_t _dapPort;
    DapTransportMode _dapTransport;
    const DebugServer& _server;
};

} // namespace noix::debug
