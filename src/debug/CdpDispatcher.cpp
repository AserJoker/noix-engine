#include "debug/CdpDispatcher.h"

namespace noix::debug {

void CdpDispatcher::registerHandler(const std::string& method, Handler handler) {
    _handlers[method] = std::move(handler);
}

CdpDispatcher::Handler CdpDispatcher::findHandler(const std::string& method) const {
    auto it = _handlers.find(method);
    if (it != _handlers.end()) return it->second;
    return nullptr;
}

} // namespace noix::debug
