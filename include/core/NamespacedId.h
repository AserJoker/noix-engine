#pragma once

#include <string>
#include <string_view>

namespace noix::core {

class NamespacedId {
public:
    static constexpr std::string_view kDefaultNamespace = "noix";

    /// Parse "namespace:name" or "name" (defaults to noix namespace).
    /// Throws std::invalid_argument on empty/invalid input.
    static NamespacedId parse(std::string_view str);

    NamespacedId() = default;
    explicit NamespacedId(std::string_view name);
    NamespacedId(std::string_view ns, std::string_view name);

    const std::string& ns() const { return _ns; }
    const std::string& name() const { return _name; }

    std::string toString() const;

    bool operator==(const NamespacedId& other) const;
    bool operator!=(const NamespacedId& other) const;
    bool operator<(const NamespacedId& other) const;

private:
    std::string _ns{kDefaultNamespace};
    std::string _name;
};

} // namespace noix::core
