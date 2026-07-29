#include "debug/DapServer.h"
#include "debug/DapBridge.h"
#include "DapBridgeUtils.h"
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
    if (_useStdio) {
        /* stdio mode: single connection, no reconnection support */
        init_stdio_transport(&_bridge.transport);
        core::Logger::instance().info("DapServer: stdio transport initialized");
        _bridge.startHandlerThread();
        _readerThread = std::thread(&DapServer::readerThreadFunc, this);
    } else if (_port > 0) {
        /* TCP mode: create server socket, accept connections in a loop */
        if (!NET_Init()) {
            core::Logger::instance().error("NET_Init failed: {}", SDL_GetError());
            return;
        }
        _tcpCtx->server = NET_CreateServer(nullptr, _port, 0);
        if (!_tcpCtx->server) {
            core::Logger::instance().error("NET_CreateServer failed: {}", SDL_GetError());
            NET_Quit();
            return;
        }
        _tcpCtx->shuttingDown = &_bridge.shuttingDown;
        _tcpCtx->recvBuffer.clear();

        core::Logger::instance().info("DapServer: listening on port {}", _port);

        /* Start handler thread */
        _bridge.startHandlerThread();

        /* Start reader thread (handles accept + read loop) */
        _readerThread = std::thread(&DapServer::readerThreadFunc, this);
    } else {
        core::Logger::instance().warn("DapServer: no transport configured (no --dap-port or --dap-stdio)");
    }
}

void DapServer::stop() {
    core::Logger::instance().info("DapServer::stop: begin");
    _bridge.shuttingDown = true;

    /* If the script thread is paused in a debug callback, nudge drainQueue
       so it notices shuttingDown and calls JS_DebugContinue on the script
       thread. We must NOT call JS_DebugContinue directly here — it must
       only be called from the script thread. */
    if (_bridge.rt && JS_DebugGetState(_bridge.rt) != 0) {
        {
            std::lock_guard<std::mutex> lk(_bridge.cmdMutex);
            /* Push a no-op so drainQueue wakes up and checks shuttingDown */
            _bridge.cmdQueue.push([](){});
        }
        _bridge.cmdCv.notify_one();
    }

    /* Close client socket to unblock the reader thread's tcp_read_byte */
    _bridge.closeTransport();

    /* Close the server socket to unblock NET_AcceptClient in the reader thread.
       Without this, the reader thread stays blocked waiting for a new client
       and join() hangs. */
    if (!_useStdio && _tcpCtx && _tcpCtx->server) {
        NET_DestroyServer(_tcpCtx->server);
        _tcpCtx->server = nullptr;
    }

    /* Wake up any threads waiting on condition variables */
    _bridge.cmdCv.notify_one();
    _bridge.reqCv.notify_one();

    core::Logger::instance().info("DapServer::stop: joining reader thread...");
    if (_readerThread.joinable()) {
        _readerThread.join();
    }
    core::Logger::instance().info("DapServer::stop: reader thread joined");

    core::Logger::instance().info("DapServer::stop: stopping handler thread...");
    _bridge.stopHandlerThread();
    core::Logger::instance().info("DapServer::stop: handler thread stopped");

    /* Resume the game loop in case it's frozen */
    _bridge.resumeGameLoop();

    /* Final TCP cleanup (NET_Quit) */
    if (!_useStdio && _tcpCtx) {
        /* Server socket already destroyed above; just clean up NET */
        NET_Quit();
    }
    core::Logger::instance().info("DapServer::stop: done");
}

void DapServer::readerThreadFunc() {
    if (_useStdio) {
        /* stdio: single connection, exit when client disconnects */
        std::string message;
        while (dap_read_message(_bridge.transport, message)) {
            {
                std::lock_guard<std::mutex> lk(_bridge.reqMutex);
                _bridge.reqQueue.push(std::move(message));
            }
            _bridge.reqCv.notify_one();
            message.clear();
        }
        core::Logger::instance().info("DapServer: reader thread exited (stdio)");
        return;
    }

    /* TCP: accept connections in a loop, supporting reconnection */
    while (!_bridge.shuttingDown.load()) {
        /* Wait for a client connection */
        core::Logger::instance().info("DapServer: waiting for client on port {}...", _port);
        if (!tcp_accept_client(_tcpCtx.get())) {
            core::Logger::instance().info("DapServer: accept aborted (shutting down)");
            break;
        }

        core::Logger::instance().info("DapServer: client connected on port {}", _port);

        /* Reset session state for the new connection */
        _bridge.resetSession();

        /* Set up transport callbacks for this client */
        _bridge.transport.readByte = tcp_read_byte;
        _bridge.transport.writeMessage = tcp_write_message;
        _bridge.transport.ctx = _tcpCtx.get();

        /* Read messages until client disconnects */
        std::string message;
        while (dap_read_message(_bridge.transport, message)) {
            {
                std::lock_guard<std::mutex> lk(_bridge.reqMutex);
                _bridge.reqQueue.push(std::move(message));
            }
            _bridge.reqCv.notify_one();
            message.clear();
        }

        core::Logger::instance().info("DapServer: client disconnected");

        /* Close client socket and prepare for next connection */
        _bridge.closeTransport();
        _tcpCtx->recvBuffer.clear();
    }

    core::Logger::instance().info("DapServer: reader thread exited (TCP)");
}

} // namespace noix::debug
