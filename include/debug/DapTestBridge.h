#pragma once

/*
 * DapTestBridge — Minimal DAP debug bridge for QuickJS.
 *
 * Communicates over stdin/stdout using the DAP wire protocol
 * (Content-Length header + JSON body). Provides just enough
 * DAP functionality to verify the QuickJS debug API enhancements.
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
