#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace noix::core {

/// Semantic version: major.minor.patch
struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;

    static SemVer parse(const std::string& str);
    std::string toString() const;
};

/// Version range following npm conventions: ^1.2.3, ~1.0.0, >=2.0.0, 1.2.3 (exact)
struct VersionRange {
    enum class Op { Exact, Caret, Tilde, Gte };

    Op op = Op::Exact;
    SemVer version;

    static VersionRange parse(const std::string& str);

    /// Check if a given version satisfies this range
    bool satisfies(const SemVer& ver) const;
};

} // namespace noix::core
