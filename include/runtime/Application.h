#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include "core/ArgsParser.h"

namespace noix::debug { class DebugServer; }
namespace noix::resource { class ResourcePack; }
namespace noix::script { class JSEngine; }

struct SDL_Window;

namespace noix::runtime {

enum class RunMode { ServerOnly, Full };

class Application {
public:
    Application(int argc, char* argv[]);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run();

    const std::string& basePath() const { return _basePath; }
    const core::ArgsParser& args() const { return _args; }
    RunMode runMode() const { return _runMode; }
    bool isHeadless() const { return _runMode == RunMode::ServerOnly; }
    resource::ResourcePack& resourcePack() const { return *_resourcePack; }

private:
    bool initCore();
    bool initWindow();
    void initDebugServer();
    void initLogger();
    void initResourcePack();
    void rotateLogs(const std::filesystem::path& logsDir);
    void cleanup();

    core::ArgsParser _args;
    std::string _basePath;
    std::unique_ptr<debug::DebugServer> _debugServer;
    std::unique_ptr<resource::ResourcePack> _resourcePack;
    std::unique_ptr<script::JSEngine> _jsEngine;
    SDL_Window* _window = nullptr;
    std::atomic<bool> _running{false};
    uint32_t _shutdownEventType = 0;
    RunMode _runMode = RunMode::Full;
    bool _sdlInitialized = false;
    bool _cleanedUp = false;
};

} // namespace noix::runtime
