#pragma once

/*
 * DapSocket — Thread-safe TCP socket wrapper for DAP transport.
 *
 * Provides safe read/write operations with automatic state checking.
 * After close(), all read/write calls return immediately without crashing.
 * Write operations are internally mutex-protected for multi-thread use.
 */

#include <SDL3_net/SDL_net.h>

#include <atomic>
#include <mutex>
#include <string>

namespace noix::debug {

class DapSocket {
public:
    DapSocket() = default;
    ~DapSocket();

    DapSocket(const DapSocket&) = delete;
    DapSocket& operator=(const DapSocket&) = delete;

    /* ---- Server lifecycle ---- */

    /* Create a server socket and start listening. Returns false on error. */
    bool listen(uint16_t port, std::atomic<bool> &shuttingDown);

    /* Block until a client connects or shuttingDown is set. Returns true on success. */
    bool acceptClient();

    /* Destroy the server socket (after all clients are done). */
    void closeServer();

    /* ---- Client read/write ---- */

    /* Read a single byte. Returns -1 on EOF/error/disconnect. */
    int readByte();

    /* Write a complete DAP wire message. Safe to call from any thread.
       Returns false if the socket is closed/disconnected. */
    bool writeMessage(const std::string &msg);

    /* ---- Client lifecycle ---- */

    /* Close the client socket. After this, readByte returns -1 and
       writeMessage returns false. Safe to call multiple times.
       Must be called from outside any writeMessage call (no re-entrant lock). */
    void closeClient();

    /* Reset client state for a new connection (after closeClient). */
    void resetClient();

    /* ---- State queries ---- */

    bool isConnected() const { return _client != nullptr && !_closed.load(); }
    bool isShuttingDown() const { return _shuttingDown && _shuttingDown->load(); }

    /* ---- DAP wire protocol ---- */

    /* Read a complete DAP message (headers + body). Returns false on EOF. */
    bool readDapMessage(std::string &out);

    /* Write a DAP message with Content-Length header. Thread-safe. */
    bool writeDapMessage(const std::string &json);

    /* Mutex for write operations — allows multiple threads to send
       DAP events/responses without corrupting the stream.
       Public so that closeTransport can lock it before closing. */
    std::mutex _writeMutex;

private:
    NET_Server *_server = nullptr;
    NET_StreamSocket *_client = nullptr;
    std::string _recvBuffer;
    std::atomic<bool> *_shuttingDown = nullptr;

    /* Socket state: set by closeClient(), checked by all I/O operations.
       Once true, no socket I/O will be attempted. */
    std::atomic<bool> _closed{false};
};

} // namespace noix::debug
