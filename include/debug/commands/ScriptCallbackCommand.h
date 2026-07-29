#pragma once

/*
 * ScriptCallbackCommand — a Command that delegates execution to
 * ScriptEngine::invokeCallback(). The JS callback is owned by
 * ScriptEngine; this class only holds the endpoint name and an
 * engine reference.
 */

#include "debug/Command.h"
#include "script/ScriptEngine.h"

namespace noix::debug {

class ScriptCallbackCommand : public Command {
public:
    ScriptCallbackCommand(const std::string& name, const std::string& version,
                          script::ScriptEngine& engine);
    ~ScriptCallbackCommand() = default;

    ScriptCallbackCommand(const ScriptCallbackCommand&) = delete;
    ScriptCallbackCommand& operator=(const ScriptCallbackCommand&) = delete;

    std::string name() const override { return _name; }
    std::string version() const override { return _version; }
    std::string description() const override { return "Script-registered endpoint"; }

    Value requestSchema() const override { return Value::object(); }
    Value responseSchema() const override { return Value::object(); }

    Value execute(const Value& request) override;

private:
    std::string _name;
    std::string _version;
    script::ScriptEngine& _engine;
};

} // namespace noix::debug
