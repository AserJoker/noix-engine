#include "debug/DapServer.h"
#include "debug/DapBridge.h"
#include "script/ScriptEngine.h"
#include "core/Logger.h"

#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>

namespace noix::debug {

DapServer::DapServer(uint16_t port, script::ScriptEngine& engine, bool useStdio)
    : _port(port)
    , _useStdio(useStdio)
    , _engine(engine)
    , _bridge()
    , _tcpCtx(std::make_unique<TcpCtx>())
{
    _bridge.setEngine(&engine);
}

DapServer::~DapServer() {
    stop();
}

void DapServer::start() {
    /* Initialize transport */
    if (_useStdio) {
        init_stdio_transport(&_bridge.transport);
        core::Logger::instance().info("DapServer: stdio transport initialized");
    } else if (_port > 0) {
        if (!init_tcp_transport(&_bridge.transport, _tcpCtx.get(), _port, _bridge.shuttingDown)) {
            core::Logger::instance().error("DapServer: failed to initialize TCP transport on port {}", _port);
            return;
        }
        core::Logger::instance().info("DapServer: TCP transport initialized on port {}", _port);
    } else {
        core::Logger::instance().warn("DapServer: no transport configured (no --dap-port or --dap-stdio)");
        return;
    }

    /* Start handler thread */
    _bridge.startHandlerThread();

    /* Start reader thread */
    _readerThread = std::thread(&DapServer::readerThreadFunc, this);
}

void DapServer::stop() {
    _bridge.shuttingDown = true;

    /* If the script thread is paused in a debug callback, resume QuickJS so
       it can exit the paused loop. Without this, drainQueue() blocks forever
       on cmdCv and the script thread never checks _running. */
    if (_bridge.rt) {
        JS_DebugContinue(_bridge.rt);
    }

    /* Close the client socket to unblock the reader thread.
       If handleDisconnect already closed it, this is a no-op (client is null). */
    _bridge.closeTransport();

    /* Wake up any threads waiting on condition variables */
    _bridge.cmdCv.notify_one();
    _bridge.reqCv.notify_one();

    if (_readerThread.joinable()) {
        _readerThread.join();
    }

    _bridge.stopHandlerThread();

    /* Resume the game loop in case it's frozen */
    _bridge.resumeGameLoop();

    /* Cleanup TCP (client socket may already be closed by closeTransport) */
    if (!_useStdio && _tcpCtx) {
        cleanup_tcp(_tcpCtx.get());
    }
}

void DapServer::readerThreadFunc() {
    std::string message;

    while (dap_read_message(_bridge.transport, message)) {
        {
            std::lock_guard<std::mutex> lk(_bridge.reqMutex);
            _bridge.reqQueue.push(std::move(message));
        }
        _bridge.reqCv.notify_one();
        message.clear();
    }

    core::Logger::instance().info("DapServer: reader thread exited");
}

} // namespace noix::debug
