#include "core/SemVer.h"
#include <cstdlib>
#include <sstream>
#include <vector>

namespace noix::core {

SemVer SemVer::parse(const std::string& str) {
    SemVer sv;
    // Strip leading 'v' if present
    std::string s = str;
    if (!s.empty() && s[0] == 'v') s = s.substr(1);

    // Split by '.'
    std::vector<std::string> parts;
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, '.')) {
        parts.push_back(token);
    }

    if (parts.size() >= 1) sv.major = std::atoi(parts[0].c_str());
    if (parts.size() >= 2) sv.minor = std::atoi(parts[1].c_str());
    if (parts.size() >= 3) sv.patch = std::atoi(parts[2].c_str());

    return sv;
}

std::string SemVer::toString() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

VersionRange VersionRange::parse(const std::string& str) {
    VersionRange range;
    std::string s = str;

    if (s.size() >= 1 && s[0] == '^') {
        range.op = Op::Caret;
        range.version = SemVer::parse(s.substr(1));
    } else if (s.size() >= 1 && s[0] == '~') {
        range.op = Op::Tilde;
        range.version = SemVer::parse(s.substr(1));
    } else if (s.size() >= 2 && s[0] == '>' && s[1] == '=') {
        range.op = Op::Gte;
        range.version = SemVer::parse(s.substr(2));
    } else {
        range.op = Op::Exact;
        range.version = SemVer::parse(s);
    }

    return range;
}

bool VersionRange::satisfies(const SemVer& ver) const {
    switch (op) {
    case Op::Exact:
        // Exact match: major.minor.patch must be equal
        return ver.major == version.major &&
               ver.minor == version.minor &&
               ver.patch == version.patch;

    case Op::Caret:
        // ^1.2.3 := >=1.2.3 <2.0.0 (same major)
        // ^0.2.3 := >=0.2.3 <0.3.0 (major=0, same minor)
        // ^0.0.3 := >=0.0.3 <0.0.4 (major=0, minor=0, exact patch)
        if (ver.major != version.major) return false;
        if (version.major == 0) {
            if (ver.minor != version.minor) return false;
            return ver.patch >= version.patch;
        }
        if (ver.minor < version.minor) return false;
        if (ver.minor == version.minor && ver.patch < version.patch) return false;
        return true;

    case Op::Tilde:
        // ~1.2.3 := >=1.2.3 <1.3.0 (same major.minor)
        if (ver.major != version.major) return false;
        if (ver.minor != version.minor) return false;
        return ver.patch >= version.patch;

    case Op::Gte:
        // >=1.2.3 — simply check ver >= version
        if (ver.major > version.major) return true;
        if (ver.major < version.major) return false;
        if (ver.minor > version.minor) return true;
        if (ver.minor < version.minor) return false;
        return ver.patch >= version.patch;
    }

    return false;
}

} // namespace noix::core
