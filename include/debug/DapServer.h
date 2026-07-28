#pragma once

#include "debug/DapBridge.h"

#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace noix::script { class ScriptEngine; }

namespace noix::debug {

class DapServer {
public:
    DapServer(uint16_t port, script::ScriptEngine& engine, bool useStdio = false);
    ~DapServer();

    DapServer(const DapServer&) = delete;
    DapServer& operator=(const DapServer&) = delete;

    void start();
    void stop();

    uint16_t port() const { return _port; }
    DapBridge& bridge() { return _bridge; }

private:
    void readerThreadFunc();

    uint16_t _port;
    bool _useStdio;
    script::ScriptEngine& _engine;
    DapBridge _bridge;
    std::unique_ptr<TcpCtx> _tcpCtx;
    std::thread _readerThread;
};

} // namespace noix::debug
