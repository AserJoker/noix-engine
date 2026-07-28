#pragma once

#include "debug/Command.h"
#include "debug/HttpServer.h"

namespace noix::debug {

class SchemaCommand : public Command {
public:
    explicit SchemaCommand(const HttpServer& server) : _server(server) {}

    std::string name() const override { return "system/schema"; }
    std::string description() const override { return "Query JSON Schema for an API endpoint"; }

    Value requestSchema() const override {
        return Value::object({
            {"type", Value("object")},
            {"properties", Value::object({
                {"name", Value::object({{"type", Value("string")}, {"description", Value("Endpoint name")}})}
            })}
        });
    }

    Value responseSchema() const override {
        return Value::object({
            {"type", Value("object")},
            {"properties", Value::object({
                {"name", Value::object({{"type", Value("string")}})},
                {"method", Value::object({{"type", Value("string")}})},
                {"path", Value::object({{"type", Value("string")}})},
                {"description", Value::object({{"type", Value("string")}})},
                {"request", Value::object({{"type", Value("object")}})},
                {"response", Value::object({{"type", Value("object")}})}
            })}
        });
    }

    Value execute(const Value& request) override {
        std::string endpointName = request.has("name") ? request["name"].asString() : "";

        if (endpointName.empty()) {
            return Value::object({{"error", Value("missing 'name' field")}});
        }

        const Command* cmd = _server.findApi(endpointName);
        if (!cmd) {
            return Value::object({{"error", Value("unknown endpoint")}, {"name", Value(endpointName)}});
        }

        return Value::object({
            {"name", Value(cmd->name())},
            {"method", Value("POST")},
            {"path", Value("/api/v1/" + cmd->name())},
            {"description", Value(cmd->description())},
            {"request", cmd->requestSchema()},
            {"response", cmd->responseSchema()}
        });
    }

private:
    const HttpServer& _server;
};

} // namespace noix::debug
