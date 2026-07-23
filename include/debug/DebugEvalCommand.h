#pragma once

#include "debug/Command.h"

namespace noix::script { class ScriptEngine; }

namespace noix::debug {

class DebugEvalCommand : public Command {
public:
    explicit DebugEvalCommand(script::ScriptEngine& engine);
    std::string execute(const std::string& arguments) override;

private:
    script::ScriptEngine& _engine;
};

} // namespace noix::debug
