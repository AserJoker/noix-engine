#include "debug/commands/ScriptCallbackCommand.h"

namespace noix::debug {

ScriptCallbackCommand::ScriptCallbackCommand(const std::string& name, const std::string& version,
                                             script::ScriptEngine& engine)
    : _name(name), _version(version), _engine(engine) {}

Value ScriptCallbackCommand::execute(const Value& request) {
    return _engine.invokeCallback(_name, request);
}

} // namespace noix::debug
