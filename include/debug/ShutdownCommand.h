#pragma once

#include "debug/Command.h"
#include <cstdint>

namespace noix::debug {

class ShutdownCommand : public Command {
public:
    explicit ShutdownCommand(uint32_t shutdownEventType);
    std::string execute(const std::string& arguments) override;

private:
    uint32_t _shutdownEventType;
};

} // namespace noix::debug
