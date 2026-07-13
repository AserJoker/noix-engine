#pragma once

#include "debug/Command.h"

namespace noix::script { class JSEngine; }

namespace noix::debug {

class EvalCommand : public Command {
public:
    explicit EvalCommand(script::JSEngine& engine);
    std::string execute(const std::string& arguments) override;

private:
    script::JSEngine& _engine;
};

} // namespace noix::debug
