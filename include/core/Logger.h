#pragma once

#include "core/Sink.h"
#include <atomic>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace noix::core {

class Logger {
public:
    static Logger& instance();

    void addSink(std::shared_ptr<Sink> sink);
    void removeSink(const std::shared_ptr<Sink>& sink);
    void setLevel(LogLevel level);
    LogLevel level() const { return _level.load(); }

    void installSdlRedirect();
    void restoreSdlOutput();

    template<typename... Args>
    void log(LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
        if (level < _level.load()) return;
        auto msg = std::format(fmt, std::forward<Args>(args)...);
        dispatch(level, msg);
    }

    template<typename... Args>
    void trace(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Trace, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Info, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Warn, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Error, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void critical(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Critical, fmt, std::forward<Args>(args)...);
    }

private:
    Logger() = default;
    void dispatch(LogLevel level, const std::string& message);

    std::vector<std::shared_ptr<Sink>> _sinks;
    std::atomic<LogLevel> _level{LogLevel::Info};
    std::mutex _mutex;
};

} // namespace noix::core
