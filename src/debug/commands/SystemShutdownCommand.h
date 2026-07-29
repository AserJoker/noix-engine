#pragma once

#include "debug/Command.h"
#include "core/Value.h"

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
        return R"({
            "type": "object",
            "properties": {
                "status": {"type": "string", "enum": ["shutting-down", "already-shutting-down"]}
            }
        })"_json;
    }

    Value execute(const Value& /*request*/) override {
        if (_shutdownRequested.exchange(true)) {
            return R"({"status": "already-shutting-down"})"_json;
        }

        if (_shutdownCallback) {
            _shutdownCallback();
        }

        return R"({"status": "shutting-down"})"_json;
    }

private:
    std::function<void()> _shutdownCallback;
    std::atomic<bool> _shutdownRequested;
};

} // namespace noix::debug
