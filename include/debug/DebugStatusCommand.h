#pragma once

#include "debug/Command.h"

namespace noix::script { class ScriptEngine; }

namespace noix::debug {

class DebugStatusCommand : public Command {
public:
    explicit DebugStatusCommand(script::ScriptEngine& engine);
    std::string execute(const std::string& arguments) override;

private:
    script::ScriptEngine& _engine;
};

} // namespace noix::debug
