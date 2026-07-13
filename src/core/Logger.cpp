#include "core/Logger.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <ctime>

namespace noix::core {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::addSink(std::shared_ptr<Sink> sink) {
    std::lock_guard lock(_mutex);
    _sinks.push_back(std::move(sink));
}

void Logger::removeSink(const std::shared_ptr<Sink>& sink) {
    std::lock_guard lock(_mutex);
    auto it = std::find(_sinks.begin(), _sinks.end(), sink);
    if (it != _sinks.end()) {
        _sinks.erase(it);
    }
}

void Logger::setLevel(LogLevel level) {
    _level.store(level);
}

void Logger::dispatch(LogLevel level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    char timeBuf[24];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tm);
    std::string formatted = std::format("{}.{:03d} {}", timeBuf, ms.count(), message);

    std::lock_guard lock(_mutex);
    for (auto& sink : _sinks) {
        sink->write(level, formatted);
    }
}

void Logger::installSdlRedirect() {
    struct Redirect {
        static void SDLCALL callback(void*, int, SDL_LogPriority priority, const char* message) {
            LogLevel level = LogLevel::Info;
            switch (priority) {
            case SDL_LOG_PRIORITY_VERBOSE:  level = LogLevel::Trace; break;
            case SDL_LOG_PRIORITY_DEBUG:    level = LogLevel::Debug; break;
            case SDL_LOG_PRIORITY_INFO:     level = LogLevel::Info; break;
            case SDL_LOG_PRIORITY_WARN:     level = LogLevel::Warn; break;
            case SDL_LOG_PRIORITY_ERROR:    level = LogLevel::Error; break;
            case SDL_LOG_PRIORITY_CRITICAL: level = LogLevel::Critical; break;
            default: break;
            }
            Logger::instance().dispatch(level, message);
        }
    };
    SDL_SetLogOutputFunction(Redirect::callback, nullptr);
}

void Logger::restoreSdlOutput() {
    SDL_SetLogOutputFunction(SDL_GetDefaultLogOutputFunction(), nullptr);
}

} // namespace noix::core
