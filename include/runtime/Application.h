#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include "core/ArgsParser.h"

#include <csignal>

namespace noix::core { class ConfigManager; }
namespace noix::debug { class HttpServer; class DapServer; }
namespace noix::resource { class ResourcePack; }
namespace noix::script { class ScriptEngine; }

struct SDL_Window;

namespace noix::runtime {

enum class RunMode { ServerOnly, Full };

enum class WindowMode {
    Windowed,          // 有边框窗口
    Borderless,        // 无边框窗口（全屏窗口）
    Fullscreen         // 独占全屏
};

enum class Resolution {
    HD,       // 1280x720
    FHD,      // 1920x1080
    QHD,      // 2560x1440
    UHD       // 3840x2160
};

struct ResolutionSize {
    int width;
    int height;
};

ResolutionSize getResolutionSize(Resolution res);
const char* toString(WindowMode mode);
WindowMode parseWindowMode(const std::string& str);
Resolution parseResolution(const std::string& str);
const char* toString(Resolution res);

class Application {
public:
    Application(int argc, char* argv[]);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    static Application& instance();

private:
    static Application* _instance;
    static void signalHandler(int sig);

public:
    static void requestShutdown();

public:
    int run();

    const std::string& basePath() const { return _basePath; }
    const core::ArgsParser& args() const { return _args; }
    core::ConfigManager& configManager() const { return *_configManager; }
    RunMode runMode() const { return _runMode; }
    bool isHeadless() const { return _runMode == RunMode::ServerOnly; }
    resource::ResourcePack& resourcePack() const { return *_resourcePack; }
    script::ScriptEngine& scriptEngine() const { return *_scriptEngine; }

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
    std::unique_ptr<core::ConfigManager> _configManager;
    std::unique_ptr<debug::HttpServer> _httpServer;
    std::unique_ptr<debug::DapServer> _dapServer;
    std::unique_ptr<resource::ResourcePack> _resourcePack;
    std::unique_ptr<script::ScriptEngine> _scriptEngine;
    SDL_Window* _window = nullptr;
    std::atomic<bool> _running{false};
    std::atomic<bool> _frozen{false};
    uint32_t _shutdownEventType = 0;
    uint32_t _freezeEventType = 0;
    uint32_t _resumeEventType = 0;
    RunMode _runMode = RunMode::Full;
    bool _sdlInitialized = false;
    bool _cleanedUp = false;
};

} // namespace noix::runtime
