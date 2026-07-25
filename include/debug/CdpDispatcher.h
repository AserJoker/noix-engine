#pragma once

#include <functional>
#include <string>
#include <unordered_map>

struct cJSON;

namespace noix::debug {

/// Dispatches CDP method strings to registered handler functions.
class CdpDispatcher {
public:
    using Handler = std::function<cJSON*(const cJSON* params)>;

    void registerHandler(const std::string& method, Handler handler);
    Handler findHandler(const std::string& method) const;

private:
    std::unordered_map<std::string, Handler> _handlers;
};

} // namespace noix::debug
