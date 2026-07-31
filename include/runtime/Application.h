#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include "core/ArgsParser.h"

#include <SDL3/SDL.h>
#include <csignal>

namespace noix::debug { class DebugServer; class DapServer; }
namespace noix::runtime { class AssetManager; class ConfigManager; class EventBus; class LocaleManager; class ModManager; class SaveManager; }
namespace noix::script { class ScriptEngine; }
namespace noix::video { class Renderer; }

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
    ConfigManager& configManager() const { return *_configManager; }
    RunMode runMode() const { return _runMode; }
    bool isHeadless() const { return _runMode == RunMode::ServerOnly; }
    AssetManager& assetManager() const { return *_assetManager; }
    LocaleManager& localeManager() const { return *_localeManager; }
    SaveManager& saveManager() const { return *_saveManager; }
    script::ScriptEngine& scriptEngine() const { return *_scriptEngine; }
    EventBus& eventBus() const { return *_eventBus; }
    ModManager& modManager() const { return *_modManager; }
    video::Renderer& renderer() const { return *_renderer; }

private:
    bool initCore();
    bool initWindow();
    void initDebugServer();
    void initLogger();
    void initResourcePack();
    void rotateLogs(const std::filesystem::path& logsDir);
    void cleanup();

    using EventHandler = std::function<void(const SDL_Event&)>;
    void registerEventHandler(uint32_t eventType, EventHandler handler);

    core::ArgsParser _args;
    std::string _basePath;
    std::unique_ptr<ConfigManager> _configManager;
    std::unique_ptr<debug::DebugServer> _debugServer;
    std::unique_ptr<debug::DapServer> _dapServer;
    std::unique_ptr<EventBus> _eventBus;
    std::unique_ptr<ModManager> _modManager;
    std::unique_ptr<AssetManager> _assetManager;
    std::unique_ptr<LocaleManager> _localeManager;
    std::unique_ptr<SaveManager> _saveManager;
    std::unique_ptr<script::ScriptEngine> _scriptEngine;
    std::unique_ptr<video::Renderer> _renderer;
    SDL_Window* _window = nullptr;
    std::atomic<bool> _running{false};
    std::atomic<bool> _frozen{false};
    std::unordered_map<uint32_t, EventHandler> _eventHandlers;
    RunMode _runMode = RunMode::Full;
    bool _sdlInitialized = false;
    bool _cleanedUp = false;
};

} // namespace noix::runtime
