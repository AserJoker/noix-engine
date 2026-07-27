#pragma once

/*
 * DapTestBridge — Minimal DAP debug bridge for QuickJS.
 *
 * Communicates over stdin/stdout or TCP socket using the DAP wire protocol
 * (Content-Length header + JSON body). Supports VS Code launch mode (stdio)
 * and attach mode (TCP via --port).
 */

#include "quickjs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Run the DAP test bridge. Blocks until disconnect. */
int dap_test_bridge_main(int argc, char **argv);

#ifdef __cplusplus
}
#endif
