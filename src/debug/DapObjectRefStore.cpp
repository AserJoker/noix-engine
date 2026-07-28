#include "debug/DapObjectRefStore.h"

namespace noix::debug {

int ObjectRefStore::add(JSValue obj) {
    int ref = _nextVarRef++;
    _refs.push_back({ref, obj});
    return ref;
}

JSValue ObjectRefStore::find(int varRef) const {
    for (auto &entry : _refs) {
        if (entry.varRef == varRef)
            return entry.obj;
    }
    return JS_UNDEFINED;
}

void ObjectRefStore::clear(JSContext *ctx) {
    for (auto &entry : _refs) {
        if (ctx) JS_FreeValue(ctx, entry.obj);
    }
    _refs.clear();
    _nextVarRef = 100000;
}

} // namespace noix::debug
