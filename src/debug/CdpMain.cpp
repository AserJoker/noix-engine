#include "debug/WebSocketServer.h"
#include "debug/JsDebugBridge.h"
#include "debug/CdpSession.h"
#include "core/Logger.h"
#include "core/Sink.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace noix::debug;

static void printUsage(const char* prog) {
    printf("Usage: %s --port <port> --script <path> [--debug-wait]\n", prog);
    printf("  --port       WebSocket server port (default: 9222)\n");
    printf("  --script     Path to JavaScript file to debug\n");
    printf("  --debug-wait Wait for debugger attach before evaluating script\n");
}

int main(int argc, char* argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Setup console logging
    auto consoleSink = std::make_shared<noix::core::ConsoleSink>();
    noix::core::Logger::instance().addSink(consoleSink);

    uint16_t port = 9222;
    std::string scriptPath;
    bool debugWait = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--script" && i + 1 < argc) {
            scriptPath = argv[++i];
        } else if (arg == "--debug-wait") {
            debugWait = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            SDL_Quit();
            return 0;
        }
    }

    if (scriptPath.empty()) {
        fprintf(stderr, "Error: --script is required\n\n");
        printUsage(argv[0]);
        SDL_Quit();
        return 1;
    }

    noix::core::Logger::instance().info("CDP debug bridge starting on port {}", port);
    noix::core::Logger::instance().info("Script: {}", scriptPath);

    WebSocketServer server(port);
    JsDebugBridge bridge;
    CdpSession session(server, bridge);

    if (!server.start()) {
        noix::core::Logger::instance().error("Failed to start WebSocket server");
        SDL_Quit();
        return 1;
    }

    if (!bridge.start(scriptPath, debugWait)) {
        noix::core::Logger::instance().error("Failed to start script");
        server.stop();
        SDL_Quit();
        return 1;
    }

    session.start();

    noix::core::Logger::instance().info("CDP bridge ready - connect with wscat or Chrome DevTools");
    printf("Connect: wscat -c ws://localhost:%d\n", port);
    printf("Or: Chrome > chrome://inspect > Configure > localhost:%d\n", port);
    printf("Press Ctrl+C to quit...\n");

    // Keep running until process is terminated
    while (true) {
        SDL_Delay(100);
    }

    session.stop();
    bridge.stop();
    server.stop();

    SDL_Quit();
    return 0;
}
