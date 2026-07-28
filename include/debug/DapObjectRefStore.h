#pragma once

/*
 * DapObjectRefStore — Tracks JS object references for DAP variable inspection.
 *
 * When the debugger needs to show an object's properties, it assigns a
 * variablesReference ID and stores the JSValue here. The Variables request
 * later looks up the object by that ID.
 *
 * Thread safety: all methods must be called from the script thread only
 * (the same thread that owns the JSContext).
 */

#include "quickjs.h"

#include <vector>

namespace noix::debug {

class ObjectRefStore {
public:
    /* Add a JSValue to the store. Returns the variablesReference ID.
       The caller must have already dup'd the value if it needs to survive. */
    int add(JSValue obj);

    /* Look up a JSValue by variablesReference. Returns JS_UNDEFINED if not found. */
    JSValue find(int varRef) const;

    /* Free all stored JSValues and reset the counter. Must be called on the
       script thread so JS_FreeValue is safe. */
    void clear(JSContext *ctx);

private:
    struct Entry {
        int varRef;
        JSValue obj;
    };
    std::vector<Entry> _refs;
    int _nextVarRef = 100000; /* start high to avoid collision with scope refs */
};

} // namespace noix::debug
