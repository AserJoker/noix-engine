#pragma once

#include "debug/DapBridge.h"

#include <cstdint>
#include <string>
#include <thread>

namespace noix::script { class ScriptEngine; }

namespace noix::debug {

class DapServer {
public:
    DapServer(uint16_t port, script::ScriptEngine& engine);
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
    script::ScriptEngine& _engine;
    DapBridge _bridge;
    std::thread _readerThread;
};

} // namespace noix::debug
