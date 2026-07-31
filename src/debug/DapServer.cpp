#include "debug/DapServer.h"
#include "debug/DapBridge.h"
#include "DapBridgeUtils.h"
#include "script/ScriptEngine.h"
#include "core/Logger.h"

#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>

namespace noix::debug {

DapServer::DapServer(uint16_t port, script::ScriptEngine& engine)
    : _port(port)
    , _engine(engine)
    , _bridge()
{
    _bridge.setEngine(&engine);
}

DapServer::~DapServer() {
    stop();
}

void DapServer::start() {
    if (_port > 0) {
        /* TCP mode: create server socket, accept connections in a loop */
        if (!NET_Init()) {
            core::Logger::instance().error("NET_Init failed: {}", SDL_GetError());
            return;
        }
        if (!_bridge.socket.listen(_port, _bridge.shuttingDown)) {
            NET_Quit();
            return;
        }

        /* Start handler thread */
        _bridge.startHandlerThread();

        /* Start reader thread (handles accept + read loop) */
        _readerThread = std::thread(&DapServer::readerThreadFunc, this);
    } else {
        core::Logger::instance().warn("DapServer: no port configured");
    }
}

void DapServer::stop() {
    core::Logger::instance().debug("DapServer::stop: begin");
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

    /* Close client socket to unblock the reader thread's readByte().
       Lock the socket's internal writeMutex to prevent racing with
       pushEvent/sendResponse which may write to the socket. */
    {
        std::lock_guard<std::mutex> lk(_bridge.socket._writeMutex);
        _bridge.closeTransport();
    }

    /* Close the server socket to unblock NET_AcceptClient in the reader thread.
       Without this, the reader thread stays blocked waiting for a new client
       and join() hangs. */
    _bridge.socket.closeServer();

    /* Wake up any threads waiting on condition variables */
    _bridge.cmdCv.notify_one();
    _bridge.reqCv.notify_one();

    core::Logger::instance().debug("DapServer::stop: joining reader thread...");
    if (_readerThread.joinable()) {
        _readerThread.join();
    }
    core::Logger::instance().debug("DapServer::stop: reader thread joined");

    core::Logger::instance().debug("DapServer::stop: stopping handler thread...");
    _bridge.stopHandlerThread();
    core::Logger::instance().debug("DapServer::stop: handler thread stopped");

    /* Resume the game loop in case it's frozen */
    _bridge.resumeGameLoop();

    NET_Quit();
    core::Logger::instance().debug("DapServer::stop: done");
}

void DapServer::readerThreadFunc() {
    /* TCP: accept connections in a loop, supporting reconnection */
    while (!_bridge.shuttingDown.load()) {
        /* Wait for a client connection */
        core::Logger::instance().info("DapServer: waiting for client on port {}...", _port);
        if (!_bridge.socket.acceptClient()) {
            core::Logger::instance().debug("DapServer: accept aborted (shutting down)");
            break;
        }

        core::Logger::instance().info("DapServer: client connected on port {}", _port);

        /* Reset session state for the new connection */
        _bridge.resetSession();

        /* Read messages until client disconnects */
        std::string message;
        while (_bridge.socket.readDapMessage(message)) {
            {
                std::lock_guard<std::mutex> lk(_bridge.reqMutex);
                _bridge.reqQueue.push(std::move(message));
            }
            _bridge.reqCv.notify_one();
            message.clear();
        }

        core::Logger::instance().info("DapServer: client disconnected");

        /* Close client socket and prepare for next connection.
           Lock the socket's writeMutex to prevent racing with
           pushEvent/sendResponse which may write to the socket. */
        {
            std::lock_guard<std::mutex> lk(_bridge.socket._writeMutex);
            _bridge.closeTransport();
        }
        _bridge.socket.resetClient();

        /* Clean up the debug session: remove breakpoints, resume the
           script if paused, reset flags. This handles the case where
           the client drops the connection without sending "disconnect".
           If handleDisconnect already ran, this is a no-op (idempotent). */
        _bridge.onClientDisconnected();
    }

    core::Logger::instance().debug("DapServer: reader thread exited (TCP)");
}

} // namespace noix::debug
