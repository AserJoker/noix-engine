#pragma once

/*
 * DapBridgeUtils.h — Internal header for DAP debug module utilities.
 *
 * This header is NOT installed. It is only included by source files within
 * the debug module that need access to transport functions, JSON helpers,
 * path helpers, module loading, and JSValue formatting.
 */

#include "quickjs.h"

#include <atomic>
#include <mutex>
#include <string>

struct cJSON;

namespace noix::debug {

class ObjectRefStore;
struct DapTransport;
struct TcpCtx;

/* ---- Transport initialization ---- */

void init_stdio_transport(DapTransport *t);
bool init_tcp_transport(DapTransport *t, TcpCtx *tcp, int port,
                         std::atomic<bool> &shuttingDown);
bool tcp_accept_client(TcpCtx *tcp);
int tcp_read_byte(void *ctx);
void tcp_write_message(void *ctx, const std::string &msg);
void cleanup_tcp(TcpCtx *tcp);

/* ---- DAP wire protocol ---- */

bool dap_read_message(DapTransport &transport, std::string &out);
void dap_write_message(DapTransport &transport, std::mutex &writeMutex,
                        const std::string &json);

/* ---- JSON helpers ---- */

cJSON *json_get(cJSON *obj, const char *key);
int json_get_int(cJSON *obj, const char *key, int def = 0);
const char *json_get_str(cJSON *obj, const char *key, const char *def = "");
bool json_get_bool(cJSON *obj, const char *key, bool def = false);

/* ---- Path helpers ---- */

const char *toAbsolutePath(const char *path);
std::string normalizePath(const char *path);

/* ---- Module support ---- */

char *dap_load_file(JSContext *ctx, size_t *pbuf_len, const char *filename);
int dap_set_import_meta(JSContext *ctx, JSValueConst func_val, bool is_main);
JSModuleDef *dap_module_loader(JSContext *ctx, const char *module_name, void *opaque);

/* ---- JSValue formatting ---- */

void formatJSValue(JSContext *ctx, ObjectRefStore &refs, cJSON *v, JSValue val,
                   const char *valueKey = "value");

} // namespace noix::debug
