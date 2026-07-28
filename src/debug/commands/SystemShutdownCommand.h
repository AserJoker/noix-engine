#pragma once

#include "debug/Command.h"

namespace noix::debug {

class SystemShutdownCommand : public Command {
public:
    explicit SystemShutdownCommand(std::function<void()> shutdownCallback)
        : _shutdownCallback(std::move(shutdownCallback)), _shutdownRequested(false) {}

    std::string name() const override { return "system/shutdown"; }
    std::string description() const override { return "Request graceful engine shutdown"; }

    Value requestSchema() const override {
        return Value::object();
    }

    Value responseSchema() const override {
        return Value::object({
            {"type", Value("object")},
            {"properties", Value::object({
                {"status", Value::object({{"type", Value("string")}, {"enum", Value::array({Value("shutting-down"), Value("already-shutting-down")})}})}
            })}
        });
    }

    Value execute(const Value& /*request*/) override {
        if (_shutdownRequested.exchange(true)) {
            return Value::object({{"status", Value("already-shutting-down")}});
        }

        if (_shutdownCallback) {
            _shutdownCallback();
        }

        return Value::object({{"status", Value("shutting-down")}});
    }

private:
    std::function<void()> _shutdownCallback;
    std::atomic<bool> _shutdownRequested;
};

} // namespace noix::debug
