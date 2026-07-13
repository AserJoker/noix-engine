#pragma once

#include <cstdint>
#include <string>

namespace noix::core {

enum class LogLevel : uint8_t {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical
};

constexpr bool operator<(LogLevel a, LogLevel b) {
    return static_cast<uint8_t>(a) < static_cast<uint8_t>(b);
}

class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(LogLevel level, const std::string& message) = 0;
    virtual void flush() {}
};

class ConsoleSink : public Sink {
public:
    void write(LogLevel level, const std::string& message) override;
    void flush() override;
};

class FileSink : public Sink {
public:
    explicit FileSink(const std::string& path);
    ~FileSink() override;

    FileSink(const FileSink&) = delete;
    FileSink& operator=(const FileSink&) = delete;

    void write(LogLevel level, const std::string& message) override;
    void flush() override;

private:
    FILE* _file = nullptr;
};

} // namespace noix::core
