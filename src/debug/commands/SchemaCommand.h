#pragma once

#include "debug/Command.h"
#include "debug/HttpServer.h"
#include "core/Value.h"

namespace noix::debug {

class SchemaCommand : public Command {
public:
    explicit SchemaCommand(const HttpServer& server) : _server(server) {}

    std::string name() const override { return "system/schema"; }
    std::string description() const override { return "Query JSON Schema for an API endpoint"; }

    Value requestSchema() const override {
        return R"({
            "type": "object",
            "properties": {
                "name": {"type": "string", "description": "Endpoint name"}
            }
        })"_json;
    }

    Value responseSchema() const override {
        return R"({
            "type": "object",
            "properties": {
                "name":        {"type": "string"},
                "method":      {"type": "string"},
                "path":        {"type": "string"},
                "description": {"type": "string"},
                "request":     {"type": "object"},
                "response":    {"type": "object"}
            }
        })"_json;
    }

    Value execute(const Value& request) override {
        std::string endpointName = request.has("name") ? request["name"].asString() : "";

        if (endpointName.empty()) {
            return R"({"error": "missing 'name' field"})"_json;
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
