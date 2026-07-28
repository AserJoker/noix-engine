#pragma once

#include "debug/Command.h"

namespace noix::debug {

class PingCommand : public Command {
public:
    std::string name() const override { return "system/ping"; }
    std::string description() const override { return "Health check"; }

    Value requestSchema() const override {
        return Value::object();
    }

    Value responseSchema() const override {
        return Value::object({
            {"type", Value("object")},
            {"properties", Value::object({
                {"status", Value::object({{"type", Value("string")}, {"enum", Value::array({Value("ok")})}})}
            })}
        });
    }

    Value execute(const Value& /*request*/) override {
        return Value::object({{"status", Value("ok")}});
    }
};

} // namespace noix::debug
