#pragma once

#include "debug/Command.h"
#include "core/Value.h"

namespace noix::debug {

class PingCommand : public Command {
public:
    std::string name() const override { return "system/ping"; }
    std::string description() const override { return "Health check"; }

    Value requestSchema() const override {
        return Value::object();
    }

    Value responseSchema() const override {
        return R"({
            "type": "object",
            "properties": {
                "status": {"type": "string", "enum": ["ok"]}
            }
        })"_json;
    }

    Value execute(const Value& /*request*/) override {
        return R"({"status": "ok"})"_json;
    }
};

} // namespace noix::debug
