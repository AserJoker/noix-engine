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
    , _tcpCtx(std::make_unique<TcpCtx>())
{
}

DapServer::~DapServer() {
    stop();
}

DapBridge& DapServer::bridge() {
    return *_engine.dapBridge();
}

void DapServer::start() {
    auto& br = bridge();

    /* Initialize transport */
    if (_useStdio) {
        init_stdio_transport(&br.transport);
        core::Logger::instance().info("DapServer: stdio transport initialized");
    } else if (_port > 0) {
        if (!init_tcp_transport(&br.transport, _tcpCtx.get(), _port, br.shuttingDown)) {
            core::Logger::instance().error("DapServer: failed to initialize TCP transport on port {}", _port);
            return;
        }
        core::Logger::instance().info("DapServer: TCP transport initialized on port {}", _port);
    } else {
        core::Logger::instance().warn("DapServer: no transport configured (no --dap-port or --dap-stdio)");
        return;
    }

    /* Start handler thread */
    br.startHandlerThread();

    /* Start reader thread */
    _readerThread = std::thread(&DapServer::readerThreadFunc, this);
}

void DapServer::stop() {
    auto& br = bridge();
    br.shuttingDown = true;
    br.reqCv.notify_one();

    if (_readerThread.joinable()) {
        _readerThread.join();
    }

    br.stopHandlerThread();

    /* Cleanup TCP */
    if (!_useStdio && _tcpCtx) {
        cleanup_tcp(_tcpCtx.get());
    }
}

void DapServer::readerThreadFunc() {
    auto& br = bridge();
    std::string message;

    while (dap_read_message(br.transport, message)) {
        {
            std::lock_guard<std::mutex> lk(br.reqMutex);
            br.reqQueue.push(std::move(message));
        }
        br.reqCv.notify_one();
        message.clear();
    }

    core::Logger::instance().info("DapServer: reader thread exited");
}

} // namespace noix::debug
