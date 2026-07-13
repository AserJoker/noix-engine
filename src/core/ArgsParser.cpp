#include "core/ArgsParser.h"

namespace noix::core {

void ArgsParser::parse(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);

        if (arg.starts_with("--")) {
            auto eq = arg.find('=');
            if (eq != std::string::npos) {
                _named[arg.substr(2, eq - 2)] = arg.substr(eq + 1);
            } else {
                std::string key = arg.substr(2);
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    _named[std::move(key)] = argv[++i];
                } else {
                    _named[std::move(key)];
                }
            }
        } else if (arg.starts_with("-")) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                _named[arg.substr(1)] = argv[++i];
            } else {
                _named[arg.substr(1)];
            }
        } else {
            _positional.push_back(arg);
        }
    }
}

bool ArgsParser::has(const std::string& key) const {
    return _named.contains(key);
}

std::string ArgsParser::get(const std::string& key, const std::string& defaultValue) const {
    auto it = _named.find(key);
    if (it != _named.end()) {
        return it->second;
    }
    return defaultValue;
}

} // namespace noix::core
