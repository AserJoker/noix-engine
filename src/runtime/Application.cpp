#include "runtime/Application.h"
#include "core/ConfigManager.h"
#include "core/Logger.h"
#include "debug/ConfigGetCommand.h"
#include "debug/DebugControlCommand.h"
#include "debug/DebugEvalCommand.h"
#include "debug/DebugEvalFrameCommand.h"
#include "debug/DebugServer.h"
#include "debug/DebugVariablesCommand.h"
#include "debug/DebugStatusCommand.h"
#include "debug/BreakpointCommand.h"
#include "debug/EvalCommand.h"
#include "debug/ShutdownCommand.h"
#include "debug/StackTraceCommand.h"
#include "debug/SourceMapCommand.h"
#include "debug/DebugRunCommand.h"
#include "resource/ResourcePack.h"
#include "script/ScriptEngine.h"
#include <SDL3/SDL.h>
#include <filesystem>

namespace noix::runtime {

Application* Application::_instance = nullptr;

ResolutionSize getResolutionSize(Resolution res) {
    switch (res) {
    case Resolution::HD:  return {1280, 720};
    case Resolution::FHD: return {1920, 1080};
    case Resolution::QHD: return {2560, 1440};
    case Resolution::UHD: return {3840, 2160};
    }
    return {1920, 1080};
}

const char* toString(WindowMode mode) {
    switch (mode) {
    case WindowMode::Windowed:   return "windowed";
    case WindowMode::Borderless: return "borderless";
    case WindowMode::Fullscreen: return "fullscreen";
    }
    return "windowed";
}

WindowMode parseWindowMode(const std::string& str) {
    if (str == "borderless") return WindowMode::Borderless;
    if (str == "fullscreen") return WindowMode::Fullscreen;
    return WindowMode::Windowed;
}

Resolution parseResolution(const std::string& str) {
    if (str == "hd")  return Resolution::HD;
    if (str == "qhd") return Resolution::QHD;
    if (str == "uhd") return Resolution::UHD;
    return Resolution::FHD;
}

const char* toString(Resolution res) {
    switch (res) {
    case Resolution::HD:  return "hd";
    case Resolution::FHD: return "fhd";
    case Resolution::QHD: return "qhd";
    case Resolution::UHD: return "uhd";
    }
    return "fhd";
}

Application::Application(int argc, char* argv[]) {
    _args.parse(argc, argv);
    if (_args.has("headless")) {
        _runMode = RunMode::ServerOnly;
    }
    if (_args.has("base-path")) {
        _basePath = _args.get("base-path");
    } else {
        const char* path = SDL_GetBasePath();
        if (path) { _basePath = path; }
    }
}

Application::~Application() { cleanup(); }

Application& Application::instance() {
    return *_instance;
}

bool Application::initCore() {
    SDL_InitFlags flags = isHeadless() ? SDL_INIT_EVENTS : SDL_INIT_VIDEO;
    if (!SDL_Init(flags)) {
        core::Logger::instance().error("SDL_Init failed: {}", SDL_GetError());
        return false;
    }
    _sdlInitialized = true;
    return true;
}

bool Application::initWindow() {
    auto cfg = _configManager->get(core::NamespacedId("noix", "application"));
    auto windowCfg = cfg.getObject("window");
    WindowMode mode = parseWindowMode(windowCfg.getString("mode", "windowed"));
    Resolution res = parseResolution(windowCfg.getString("resolution", "fhd"));
    auto [width, height] = getResolutionSize(res);

    SDL_WindowFlags flags = 0;
    if (mode == WindowMode::Fullscreen) flags = SDL_WINDOW_FULLSCREEN;
    else if (mode == WindowMode::Borderless) flags = SDL_WINDOW_BORDERLESS;

    _window = SDL_CreateWindow("noix-engine", width, height, flags);
    if (!_window) {
        core::Logger::instance().error("SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }

    // 窗口模式下允许调整大小，全屏/无边框模式下禁止
    SDL_SetWindowResizable(_window, mode == WindowMode::Windowed);

    core::Logger::instance().info("Window created ({}x{}, {})", width, height, toString(mode));
    return true;
}

void Application::rotateLogs(const std::filesystem::path& logsDir) {
    namespace fs = std::filesystem;

    fs::create_directories(logsDir);

    fs::path logFile = logsDir / "engine.log";
    if (!fs::exists(logFile)) return;

    for (int i = 9; i >= 1; --i) {
        fs::path older = logsDir / ("engine.log." + std::to_string(i));
        if (!fs::exists(older)) continue;
        if (i == 9) {
            fs::remove(older);
        } else {
            fs::rename(older, logsDir / ("engine.log." + std::to_string(i + 1)));
        }
    }

    fs::rename(logFile, logsDir / "engine.log.1");
}

void Application::initLogger() {
    auto& logger = core::Logger::instance();
    logger.addSink(std::make_shared<core::ConsoleSink>());

    std::filesystem::path logsDir = std::filesystem::path(_basePath) / "logs";
    rotateLogs(logsDir);
    logger.addSink(std::make_shared<core::FileSink>((logsDir / "engine.log").string()));

    logger.setLevel(core::LogLevel::Trace);
    logger.installSdlRedirect();
}

void Application::initResourcePack() {
    _resourcePack = std::make_unique<resource::ResourcePack>(_basePath);
    core::Logger::instance().info("ResourcePack initialized (basePath={})", _basePath);
}

void Application::initDebugServer() {
    _shutdownEventType = SDL_RegisterEvents(1);
    _freezeEventType = SDL_RegisterEvents(1);
    _resumeEventType = SDL_RegisterEvents(1);
    _scriptEngine = std::make_unique<script::ScriptEngine>(_basePath);
    _scriptEngine->setDebugEventTypes(_freezeEventType, _resumeEventType);
    if (_args.has("debug-wait")) {
        _scriptEngine->setDebugWait(true);
    }
    // TODO: 注册原生模块（需 JS 引擎确定后重新设计）
    uint16_t port = 9900;
    if (_args.has("debug-port")) port = static_cast<uint16_t>(std::stoi(_args.get("debug-port")));
    _debugServer = std::make_unique<debug::DebugServer>(port, 30);
    _debugServer->registerCommand(core::NamespacedId("noix", "exec-script"),
        std::make_unique<debug::EvalCommand>(*_scriptEngine));
    _debugServer->registerCommand(core::NamespacedId("noix", "shutdown"),
        std::make_unique<debug::ShutdownCommand>(_shutdownEventType));
    _debugServer->registerCommand(core::NamespacedId("noix", "config-get"),
        std::make_unique<debug::ConfigGetCommand>(*_configManager));
    _debugServer->registerCommand(core::NamespacedId("noix", "config-set"),
        std::make_unique<debug::ConfigSetCommand>(*_configManager));
    _debugServer->registerCommand(core::NamespacedId("noix", "config-remove"),
        std::make_unique<debug::ConfigRemoveCommand>(*_configManager));
    _debugServer->registerCommand(core::NamespacedId("noix", "config-save"),
        std::make_unique<debug::ConfigSaveCommand>(*_configManager));
    _debugServer->registerCommand(core::NamespacedId("noix", "config-list"),
        std::make_unique<debug::ConfigListCommand>(*_configManager));
    // 调试控制命令
    _debugServer->registerCommand(core::NamespacedId("noix", "debug-pause"),
        std::make_unique<debug::PauseCommand>(*_scriptEngine));
    _debugServer->registerCommand(core::NamespacedId("noix", "debug-continue"),
        std::make_unique<debug::ContinueCommand>(*_scriptEngine));
    _debugServer->registerCommand(core::NamespacedId("noix", "debug-step"),
        std::make_unique<debug::StepCommand>(*_scriptEngine));
    // 断点命令
    _debugServer->registerCommand(core::NamespacedId("noix", "debug-breakpoint-set"),
        std::make_unique<debug::BreakpointSetCommand>(*_scriptEngine));
    _debugServer->registerCommand(core::NamespacedId("noix", "debug-breakpoint-remove"),
        std::make_unique<debug::BreakpointRemoveCommand>(*_scriptEngine));
    _debugServer->registerCommand(core::NamespacedId("noix", "debug-breakpoint-clear"),
        std::make_unique<debug::BreakpointClearCommand>(*_scriptEngine));
    _debugServer->registerCommand(core::NamespacedId("noix", "debug-breakpoint-list"),
        std::make_unique<debug::BreakpointListCommand>(*_scriptEngine));
    // 检查命令
    _debugServer->registerCommand(core::NamespacedId("noix", "debug-stack-trace"),
        std::make_unique<debug::StackTraceCommand>(*_scriptEngine));
    _debugServer->registerCommand(core::NamespacedId("noix", "debug-eval"),
        std::make_unique<debug::DebugEvalCommand>(*_scriptEngine));
    _debugServer->registerCommand(core::NamespacedId("noix", "debug-status"),
        std::make_unique<debug::DebugStatusCommand>(*_scriptEngine));
    _debugServer->registerCommand(core::NamespacedId("noix", "debug-variables"),
        std::make_unique<debug::DebugVariablesCommand>(*_scriptEngine));
    _debugServer->registerCommand(core::NamespacedId("noix", "debug-eval-frame"),
        std::make_unique<debug::DebugEvalFrameCommand>(*_scriptEngine));
    _debugServer->registerCommand(core::NamespacedId("noix", "debug-source-map"),
        std::make_unique<debug::SourceMapCommand>(*_scriptEngine));
    _debugServer->registerCommand(core::NamespacedId("noix", "debug-run"),
        std::make_unique<debug::DebugRunCommand>(*_scriptEngine));
    _debugServer->start();
    core::Logger::instance().info("DebugServer started on port {}", port);
}

void Application::signalHandler(int) {
    requestShutdown();
}

void Application::requestShutdown() {
    if (_instance) {
        _instance->_running.store(false);
    }
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
static BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT ||
        ctrlType == CTRL_CLOSE_EVENT) {
        noix::runtime::Application::requestShutdown();
        return TRUE;
    }
    return FALSE;
}
#endif

int Application::run() {
    _instance = this;
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#ifdef _WIN32
    SetConsoleCtrlHandler(consoleCtrlHandler, true);
#endif
    try {
    initLogger();
    if (!initCore()) { cleanup(); return 1; }
    _configManager = std::make_unique<core::ConfigManager>(
        std::filesystem::path(_basePath) / "config");
    _configManager->loadAll();

    // 初始化 noix:application 默认配置
    {
        core::Config defaults;
        core::Config windowDefaults;
        windowDefaults.setString("mode", "windowed");
        windowDefaults.setString("resolution", "fhd");
        defaults.setObject("window", std::move(windowDefaults));
        _configManager->getOrDefault(core::NamespacedId("noix", "application"), defaults);
    }

    initResourcePack();
    if (!isHeadless()) {
        if (!initWindow()) { cleanup(); return 1; }
    }
    initDebugServer();
    _scriptEngine->start();

    core::Logger::instance().info("noix-engine started ({})",
        isHeadless() ? "server-only" : "full");
    _running.store(true);
    while (_running.load()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) _running.store(false);
            else if (event.type == _shutdownEventType) _running.store(false);
            else if (event.type == _freezeEventType) _frozen.store(true);
            else if (event.type == _resumeEventType) _frozen.store(false);
        }
        if (!_frozen.load()) {
            // 游戏逻辑刻（未来）
        }
        SDL_Delay(10);
    }
    core::Logger::instance().info("noix-engine shutting down");
    cleanup();
    return 0;
    } catch (const std::exception& e) {
        SDL_Log("EXCEPTION: %s", e.what());
        cleanup();
        return 2;
    }
}

void Application::cleanup() {
    if (_cleanedUp) return;
    _cleanedUp = true;
    _debugServer.reset();
    _resourcePack.reset();
    _scriptEngine.reset();
    if (_configManager) {
        _configManager->saveAll();
        _configManager.reset();
    }
    if (_window) { SDL_DestroyWindow(_window); _window = nullptr; }
    // 释放日志文件句柄后再 SDL_Quit
    core::Logger::instance().clearSinks();
    if (_sdlInitialized) {
        SDL_Quit();
        _sdlInitialized = false;
    }
}

} // namespace noix::runtime
