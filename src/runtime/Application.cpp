#include "runtime/Application.h"
#include "core/Logger.h"
#include "debug/DebugServer.h"
#include "debug/EvalCommand.h"
#include "debug/ShutdownCommand.h"
#include "resource/ResourcePack.h"
#include "script/JSEngine.h"
#include <SDL3/SDL.h>
#include <filesystem>

namespace noix::runtime {

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
    _window = SDL_CreateWindow("noix-engine", 800, 600, 0);
    if (!_window) {
        core::Logger::instance().error("SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }
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
    _jsEngine = std::make_unique<script::JSEngine>();
    uint16_t port = 9900;
    if (_args.has("debug-port")) port = static_cast<uint16_t>(std::stoi(_args.get("debug-port")));
    _debugServer = std::make_unique<debug::DebugServer>(port, 30);
    _debugServer->registerCommand(core::NamespacedId("debug", "eval"),
        std::make_unique<debug::EvalCommand>(*_jsEngine));
    _debugServer->registerCommand(core::NamespacedId("debug", "shutdown"),
        std::make_unique<debug::ShutdownCommand>(_shutdownEventType));
    _debugServer->start();
    core::Logger::instance().info("DebugServer started on port {}", port);
}

int Application::run() {
    try {
    initLogger();
    if (!initCore()) { cleanup(); return 1; }
    initResourcePack();
    if (!isHeadless()) {
        if (!initWindow()) { cleanup(); return 1; }
    }
    initDebugServer();

    core::Logger::instance().info("noix-engine started ({})",
        isHeadless() ? "server-only" : "full");
    _running.store(true);
    while (_running.load()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) _running.store(false);
            else if (event.type == _shutdownEventType) _running.store(false);
        }
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
    _jsEngine.reset();
    if (_window) { SDL_DestroyWindow(_window); _window = nullptr; }
    if (_sdlInitialized) {
        SDL_Quit();
        _sdlInitialized = false;
    }
}

} // namespace noix::runtime
