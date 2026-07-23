#include "debug/DebugRunCommand.h"
#include "script/ScriptEngine.h"

namespace noix::debug {

DebugRunCommand::DebugRunCommand(script::ScriptEngine& engine)
    : _engine(engine) {}

std::string DebugRunCommand::execute(const std::string&) {
    _engine.debugRun();
    return "ok";
}

} // namespace noix::debug
