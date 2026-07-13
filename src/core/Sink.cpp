#include "core/Sink.h"
#include <cstdio>

namespace noix::core {

static const char* levelTag(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:    return "TRACE";
    case LogLevel::Debug:    return "DEBUG";
    case LogLevel::Info:     return "INFO";
    case LogLevel::Warn:     return "WARN";
    case LogLevel::Error:    return "ERROR";
    case LogLevel::Critical: return "CRITICAL";
    }
    return "UNKNOWN";
}

static const char* levelColor(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:    return "\033[90m";   // gray
    case LogLevel::Debug:    return "\033[36m";   // cyan
    case LogLevel::Info:     return "\033[37m";   // white
    case LogLevel::Warn:     return "\033[33m";   // yellow
    case LogLevel::Error:    return "\033[31m";   // red
    case LogLevel::Critical: return "\033[1;31m"; // bold red
    }
    return "\033[0m";
}

static constexpr const char* RESET = "\033[0m";

// ---- ConsoleSink ----

void ConsoleSink::write(LogLevel level, const std::string& message) {
    std::fprintf(stderr, "%s[%s]%s %s\n",
        levelColor(level), levelTag(level), RESET, message.c_str());
}

void ConsoleSink::flush() {
    std::fflush(stderr);
}

// ---- FileSink ----

FileSink::FileSink(const std::string& path) {
    _file = std::fopen(path.c_str(), "a");
}

FileSink::~FileSink() {
    if (_file) {
        std::fclose(_file);
    }
}

void FileSink::write(LogLevel level, const std::string& message) {
    if (!_file) return;
    std::fprintf(_file, "[%s] %s\n", levelTag(level), message.c_str());
}

void FileSink::flush() {
    if (_file) {
        std::fflush(_file);
    }
}

} // namespace noix::core
