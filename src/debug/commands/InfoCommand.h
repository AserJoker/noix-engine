#pragma once

#include "debug/Command.h"

namespace noix::debug {

enum class DapTransportMode { None, Tcp, Stdio };

class InfoCommand : public Command {
public:
    InfoCommand(const std::string& version, uint16_t dapPort, DapTransportMode dapTransport)
        : _version(version), _dapPort(dapPort), _dapTransport(dapTransport) {}

    std::string name() const override { return "system/info"; }
    std::string description() const override { return "Engine identity, API listing, and DAP discovery"; }

    Value requestSchema() const override {
        return Value::object();
    }

    Value responseSchema() const override {
        return Value::object({
            {"type", Value("object")},
            {"properties", Value::object({
                {"name", Value::object({{"type", Value("string")}})},
                {"version", Value::object({{"type", Value("string")}})},
                {"api", Value::object({
                    {"type", Value("object")},
                    {"properties", Value::object({
                        {"v1", Value::object({{"type", Value("array")}, {"items", Value::object({{"type", Value("string")}})}})}
                    })}
                })},
                {"dap", Value::object({
                    {"type", Value("object")},
                    {"properties", Value::object({
                        {"available", Value::object({{"type", Value("boolean")}})},
                        {"transport", Value::object({{"type", Value("string")}, {"enum", Value::array({Value("tcp"), Value("stdio"), Value("none")})}})},
                        {"host", Value::object({{"type", Value("string")}})},
                        {"port", Value::object({{"type", Value("integer")}})}
                    })}
                })}
            })}
        });
    }

    Value execute(const Value& /*request*/) override {
        auto apiList = Value::array({Value("system/ping"), Value("system/info"), Value("system/schema"), Value("system/shutdown")});

        Value dapObj;
        if (_dapTransport == DapTransportMode::Tcp) {
            dapObj = Value::object({
                {"available", Value(true)},
                {"transport", Value("tcp")},
                {"host", Value("127.0.0.1")},
                {"port", Value(static_cast<int>(_dapPort))}
            });
        } else if (_dapTransport == DapTransportMode::Stdio) {
            dapObj = Value::object({
                {"available", Value(true)},
                {"transport", Value("stdio")}
            });
        } else {
            dapObj = Value::object({
                {"available", Value(false)},
                {"transport", Value("none")}
            });
        }

        return Value::object({
            {"name", Value("noix-engine")},
            {"version", Value(_version)},
            {"api", Value::object({{"v1", apiList}})},
            {"dap", dapObj}
        });
    }

private:
    std::string _version;
    uint16_t _dapPort;
    DapTransportMode _dapTransport;
};

} // namespace noix::debug
