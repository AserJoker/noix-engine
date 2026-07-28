#include "DapBridgeUtils.h"
#include "debug/DapBridge.h"
#include "core/Logger.h"

#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

namespace noix::debug {

/* ---- Stdio transport ---- */

static void init_stdio_binary() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    /* Disable Windows abort() popup -- just terminate silently */
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

static int stdio_read_byte(void *ctx) {
    (void)ctx;
    unsigned char c;
    size_t r = fread(&c, 1, 1, stdin);
    return r == 1 ? (int)c : -1;
}

static void stdio_write_message(void *ctx, const std::string &msg) {
    (void)ctx;
    fprintf(stdout, "%s", msg.c_str());
    fflush(stdout);
}

void init_stdio_transport(DapTransport *t) {
    init_stdio_binary();
    t->readByte = stdio_read_byte;
    t->writeMessage = stdio_write_message;
    t->ctx = nullptr;
}

/* ---- TCP transport ---- */

int tcp_read_byte(void *ctx) {
    auto *t = static_cast<TcpCtx *>(ctx);
    while (t->recvBuffer.empty()) {
        if (!t->client || (t->shuttingDown && t->shuttingDown->load()) || t->clientDisconnected.load()) return -1;
        /* Wait up to 100ms for data; timeout is NOT an error -- just retry.
           Short timeout ensures prompt detection of shutdown/client disconnect. */
        if (!NET_WaitUntilInputAvailable(reinterpret_cast<void **>(&t->client), 1, 100)) {
            if (!t->client || (t->shuttingDown && t->shuttingDown->load()) || t->clientDisconnected.load()) return -1;
            continue; /* Timeout -- no data yet, retry */
        }
        if (!t->client || (t->shuttingDown && t->shuttingDown->load()) || t->clientDisconnected.load()) return -1;
        char buf[4096];
        int n = NET_ReadFromStreamSocket(t->client, buf, sizeof(buf));
        if (n > 0) {
            t->recvBuffer.append(buf, n);
        } else if (n < 0) {
            return -1; /* connection closed or error */
        }
        /* n == 0: no data yet, loop and wait again */
    }
    int c = static_cast<unsigned char>(t->recvBuffer[0]);
    t->recvBuffer.erase(0, 1);
    return c;
}

void tcp_write_message(void *ctx, const std::string &msg) {
    auto *t = static_cast<TcpCtx *>(ctx);
    if (!t->client) return;
    NET_WriteToStreamSocket(t->client, msg.data(), static_cast<int>(msg.size()));
    NET_WaitUntilStreamSocketDrained(t->client, 500);
}

bool init_tcp_transport(DapTransport *t, TcpCtx *tcp, int port,
                         std::atomic<bool> &shuttingDown) {
    tcp->port = port;
    tcp->server = nullptr;
    tcp->client = nullptr;
    tcp->recvBuffer.clear();
    tcp->shuttingDown = &shuttingDown;

    if (!NET_Init()) {
        core::Logger::instance().error("NET_Init failed: {}", SDL_GetError());
        return false;
    }

    tcp->server = NET_CreateServer(nullptr, port, 0);
    if (!tcp->server) {
        core::Logger::instance().error("NET_CreateServer failed: {}", SDL_GetError());
        NET_Quit();
        return false;
    }

    core::Logger::instance().info("DAP bridge listening on port {}, waiting for connection...", port);

    if (!tcp_accept_client(tcp)) return false;

    t->readByte = tcp_read_byte;
    t->writeMessage = tcp_write_message;
    t->ctx = tcp;
    return true;
}

bool tcp_accept_client(TcpCtx *tcp) {
    tcp->clientDisconnected.store(false);
    while (!tcp->client && !(tcp->shuttingDown && tcp->shuttingDown->load())) {
        NET_AcceptClient(tcp->server, &tcp->client);
        if (!tcp->client) {
            SDL_Delay(50);
        }
    }
    return tcp->client != nullptr;
}

void cleanup_tcp(TcpCtx *tcp) {
    if (tcp->client) {
        NET_DestroyStreamSocket(static_cast<NET_StreamSocket *>(tcp->client));
        tcp->client = nullptr;
    }
    if (tcp->server) {
        NET_DestroyServer(static_cast<NET_Server *>(tcp->server));
        tcp->server = nullptr;
    }
    NET_Quit();
}

/* ---- DAP wire protocol ---- */

bool dap_read_message(DapTransport &transport, std::string &out) {
    /* Read headers until empty line (\r\n\r\n) */
    int content_length = -1;
    std::string header_buf;

    while (true) {
        int c = transport.readByte(transport.ctx);
        if (c == -1) return false;
        if (c == '\r') {
            int c2 = transport.readByte(transport.ctx);
            if (c2 == '\n') {
                /* End of header line */
                if (header_buf.empty()) {
                    /* Empty line = end of headers */
                    break;
                }
                /* Parse header */
                if (header_buf.compare(0, 15, "Content-Length:") == 0) {
                    content_length = atoi(header_buf.c_str() + 15);
                }
                header_buf.clear();
            } else {
                header_buf += (char)c;
                if (c2 != -1) header_buf += (char)c2;
            }
        } else {
            header_buf += (char)c;
        }
    }

    if (content_length <= 0) return false;

    out.resize(content_length);
    for (int i = 0; i < content_length; i++) {
        int c = transport.readByte(transport.ctx);
        if (c == -1) return false;
        out[i] = (char)c;
    }
    return true;
}

void dap_write_message(DapTransport &transport, std::mutex &writeMutex,
                        const std::string &json) {
    std::lock_guard<std::mutex> lk(writeMutex);
    std::string msg = "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n" + json;
    transport.writeMessage(transport.ctx, msg);
}

} // namespace noix::debug
