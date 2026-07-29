/*
 * DapSocket — Thread-safe TCP socket wrapper for DAP transport.
 *
 * Implementation of the safe socket wrapper declared in DapSocket.h.
 * All I/O operations check the _closed flag before touching the socket,
 * so calls after closeClient() return immediately without crashing.
 */

#include "debug/DapSocket.h"
#include "core/Logger.h"

#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>

#include <cstdio>

namespace noix::debug {

DapSocket::~DapSocket() {
    closeClient();
    closeServer();
}

/* ---- Server lifecycle ---- */

bool DapSocket::listen(uint16_t port, std::atomic<bool> &shuttingDown) {
    _shuttingDown = &shuttingDown;

    _server = NET_CreateServer(nullptr, port, 0);
    if (!_server) {
        core::Logger::instance().error("DapSocket: NET_CreateServer failed: {}", SDL_GetError());
        return false;
    }

    core::Logger::instance().info("DapSocket: listening on port {}", port);
    return true;
}

bool DapSocket::acceptClient() {
    if (!_server) return false;

    _closed.store(false);

    while (!isShuttingDown()) {
        NET_AcceptClient(_server, &_client);
        if (_client) {
            core::Logger::instance().info("DapSocket: client connected");
            return true;
        }
        SDL_Delay(50);
    }

    return false;
}

void DapSocket::closeServer() {
    if (_server) {
        NET_DestroyServer(_server);
        _server = nullptr;
    }
}

/* ---- Client read/write ---- */

int DapSocket::readByte() {
    while (_recvBuffer.empty()) {
        if (_closed.load() || !_client || isShuttingDown()) return -1;

        if (!NET_WaitUntilInputAvailable(reinterpret_cast<void **>(&_client), 1, 100)) {
            if (_closed.load() || !_client || isShuttingDown()) return -1;
            continue; /* Timeout — no data yet, retry */
        }

        if (_closed.load() || !_client || isShuttingDown()) return -1;

        char buf[4096];
        int n = NET_ReadFromStreamSocket(_client, buf, sizeof(buf));
        if (n > 0) {
            _recvBuffer.append(buf, n);
        } else if (n < 0) {
            return -1; /* Connection closed or error */
        }
        /* n == 0: no data yet, loop and wait again */
    }

    int c = static_cast<unsigned char>(_recvBuffer[0]);
    _recvBuffer.erase(0, 1);
    return c;
}

bool DapSocket::writeMessage(const std::string &msg) {
    std::lock_guard<std::mutex> lk(_writeMutex);

    if (_closed.load() || !_client) return false;

    NET_WriteToStreamSocket(_client, msg.data(), static_cast<int>(msg.size()));
    NET_WaitUntilStreamSocketDrained(_client, 500);
    return true;
}

/* ---- Client lifecycle ---- */

void DapSocket::closeClient() {
    /* Set _closed FIRST so all I/O operations return immediately.
       This must happen before destroying the socket, because other
       threads may be in readByte() or writeMessage() right now. */
    _closed.store(true);

    if (_client) {
        NET_DestroyStreamSocket(_client);
        _client = nullptr;
    }
}

void DapSocket::resetClient() {
    _closed.store(false);
    _recvBuffer.clear();
}

/* ---- DAP wire protocol ---- */

bool DapSocket::readDapMessage(std::string &out) {
    /* Read headers until empty line (\r\n\r\n) */
    int contentLength = -1;
    std::string headerBuf;

    while (true) {
        int c = readByte();
        if (c == -1) return false;

        if (c == '\r') {
            int c2 = readByte();
            if (c2 == '\n') {
                if (headerBuf.empty()) break; /* Empty line = end of headers */
                /* Parse header */
                if (headerBuf.compare(0, 15, "Content-Length:") == 0) {
                    contentLength = atoi(headerBuf.c_str() + 15);
                }
                headerBuf.clear();
            } else {
                headerBuf += static_cast<char>(c);
                if (c2 != -1) headerBuf += static_cast<char>(c2);
            }
        } else {
            headerBuf += static_cast<char>(c);
        }
    }

    if (contentLength <= 0) return false;

    out.resize(contentLength);
    for (int i = 0; i < contentLength; i++) {
        int c = readByte();
        if (c == -1) return false;
        out[i] = static_cast<char>(c);
    }
    return true;
}

bool DapSocket::writeDapMessage(const std::string &json) {
    std::string msg = "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n" + json;
    return writeMessage(msg);
}

} // namespace noix::debug
