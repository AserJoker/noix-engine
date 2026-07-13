#include "debug/ShutdownCommand.h"
#include "core/Logger.h"
#include <SDL3/SDL.h>

namespace noix::debug {

ShutdownCommand::ShutdownCommand(uint32_t shutdownEventType)
    : _shutdownEventType(shutdownEventType) {}

std::string ShutdownCommand::execute(const std::string&) {
    core::Logger::instance().info("shutdown requested");
    SDL_Event event;
    SDL_zero(event);
    event.type = _shutdownEventType;
    SDL_PushEvent(&event);
    return "shutting down";
}

} // namespace noix::debug
