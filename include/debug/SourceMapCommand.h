#pragma once

#include "debug/Command.h"
#include <string>

namespace noix::script { class ScriptEngine; }

namespace noix::debug {

class SourceMapCommand : public Command {
public:
    explicit SourceMapCommand(script::ScriptEngine& engine);
    std::string execute(const std::string& arguments) override;

private:
    script::ScriptEngine& _engine;
};

} // namespace noix::debug
