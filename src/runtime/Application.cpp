#include "runtime/Application.h"
#include "core/NamespacedId.h"
#include "core/Value.h"
#include "runtime/AssetManager.h"
#include "runtime/ConfigManager.h"
#include "runtime/EventBus.h"
#include "runtime/LocaleManager.h"
#include "runtime/SaveManager.h"
#include "core/Logger.h"
#include "debug/DebugServer.h"
#include "debug/DapServer.h"
#include "debug/commands/PingCommand.h"
#include "debug/commands/InfoCommand.h"
#include "debug/commands/SchemaCommand.h"
#include "debug/commands/SystemShutdownCommand.h"
#include "script/ScriptEngine.h"
#include <SDL3/SDL.h>
#include <filesystem>

namespace noix::runtime {

Application* Application::_instance = nullptr;

namespace {
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
} // anonymous namespace

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
    auto windowCfg = cfg["window"];
    WindowMode mode = parseWindowMode(windowCfg.has("mode") ? windowCfg["mode"].asString() : "windowed");
    Resolution res = parseResolution(windowCfg.has("resolution") ? windowCfg["resolution"].asString() : "fhd");
    auto [width, height] = getResolutionSize(res);

    SDL_WindowFlags flags = 0;
    if (mode == WindowMode::Fullscreen) flags = SDL_WINDOW_FULLSCREEN;
    else if (mode == WindowMode::Borderless) flags = SDL_WINDOW_BORDERLESS;

    _window = SDL_CreateWindow(_localeManager->i18n("noix:system.window.title","noix-engine").c_str(), width, height, flags);
    if (!_window) {
        core::Logger::instance().error("SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }

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
    _assetManager = std::make_unique<AssetManager>(_basePath);
    core::Logger::instance().info("AssetManager initialized (basePath={})", _basePath);

    _saveManager = std::make_unique<SaveManager>(_basePath);
    core::Logger::instance().info("SaveManager initialized");

    _localeManager = std::make_unique<LocaleManager>(_assetManager.get());
    _localeManager->addNamespace("noix");
    _localeManager->setLang("en_US");
}

void Application::initDebugServer() {
    _shutdownEventType = SDL_RegisterEvents(1);
    _freezeEventType = SDL_RegisterEvents(1);
    _resumeEventType = SDL_RegisterEvents(1);

    /* ScriptEngine — owns QuickJS runtime on its thread */
    _scriptEngine = std::make_unique<script::ScriptEngine>(_basePath);
    _scriptEngine->setDebugEventTypes(_freezeEventType, _resumeEventType);

    /* EventBus — event dispatch via SDL custom events */
    _eventBusEventType = SDL_RegisterEvents(1);
    _eventBus = std::make_unique<EventBus>();
    _eventBus->setEventType(_eventBusEventType);
    _eventBus->setScriptEngine(_scriptEngine.get());
    _scriptEngine->setEventBus(_eventBus.get());

    /* DapServer — DAP protocol debug server (TCP only) */
    uint16_t dapPort = 0;
    if (_args.has("dap-port")) dapPort = static_cast<uint16_t>(std::stoi(_args.get("dap-port")));

    if (dapPort > 0) {
        _dapServer = std::make_unique<debug::DapServer>(dapPort, *_scriptEngine);
        _scriptEngine->setDapBridge(&_dapServer->bridge());
    }

    /* DebugServer — REST API for operations/monitoring */
    uint16_t httpPort = 9900;
    if (_args.has("debug-port")) httpPort = static_cast<uint16_t>(std::stoi(_args.get("debug-port")));
    _debugServer = std::make_unique<debug::DebugServer>(httpPort, "/api");
    _scriptEngine->setDebugServer(_debugServer.get());

    _debugServer->addApi(std::make_shared<debug::PingCommand>());
    _debugServer->addApi(std::make_shared<debug::InfoCommand>(
        "0.1.0", dapPort, dapPort > 0 ? debug::DapTransportMode::Tcp
                                      : debug::DapTransportMode::None, *_debugServer));
    _debugServer->addApi(std::make_shared<debug::SchemaCommand>(*_debugServer));
    _debugServer->addApi(std::make_shared<debug::SystemShutdownCommand>(
        [this]() { requestShutdown(); }));

    if (dapPort > 0) _dapServer->start();
    _debugServer->start();
    core::Logger::instance().info("DebugServer started on port {}", httpPort);
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
    _configManager = std::make_unique<ConfigManager>(
        std::filesystem::path(_basePath) / "config");
    _configManager->loadAll();

    {
        core::Value defaults = core::Value::object();
        core::Value windowDefaults = core::Value::object();
        windowDefaults.asObject()["mode"] = "windowed";
        windowDefaults.asObject()["resolution"] = "fhd";
        defaults.asObject()["window"] = std::move(windowDefaults);
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
            else if (event.type == _eventBusEventType) _eventBus->handleSdlEvent(event);
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
    _dapServer.reset();
    _debugServer.reset();
    _localeManager.reset();
    _saveManager.reset();
    _assetManager.reset();
    _scriptEngine.reset();
    _eventBus.reset();
    if (_configManager) {
        _configManager->saveAll();
        _configManager.reset();
    }
    if (_window) { SDL_DestroyWindow(_window); _window = nullptr; }
    core::Logger::instance().clearSinks();
    if (_sdlInitialized) {
        SDL_Quit();
        _sdlInitialized = false;
    }
}

} // namespace noix::runtime
