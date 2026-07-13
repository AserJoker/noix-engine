#include "core/NamespacedId.h"
#include <stdexcept>

namespace noix::core {

NamespacedId NamespacedId::parse(std::string_view str) {
    auto pos = str.find(':');
    if (pos == std::string_view::npos) {
        if (str.empty()) {
            throw std::invalid_argument("invalid NamespacedId: empty string");
        }
        return NamespacedId(kDefaultNamespace, str);
    }
    auto ns = str.substr(0, pos);
    auto name = str.substr(pos + 1);
    if (ns.empty() || name.empty()) {
        throw std::invalid_argument(
            std::string("invalid NamespacedId: '") + std::string(str) + "'");
    }
    return NamespacedId(ns, name);
}

NamespacedId::NamespacedId(std::string_view name)
    : _ns(kDefaultNamespace), _name(name) {}

NamespacedId::NamespacedId(std::string_view ns, std::string_view name)
    : _ns(ns), _name(name) {}

std::string NamespacedId::toString() const {
    return _ns + ":" + _name;
}

bool NamespacedId::operator==(const NamespacedId& other) const {
    return _ns == other._ns && _name == other._name;
}

bool NamespacedId::operator!=(const NamespacedId& other) const {
    return !(*this == other);
}

bool NamespacedId::operator<(const NamespacedId& other) const {
    if (_ns != other._ns) return _ns < other._ns;
    return _name < other._name;
}

} // namespace noix::core
