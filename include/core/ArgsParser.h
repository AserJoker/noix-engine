#pragma once

#include <map>
#include <string>
#include <vector>

namespace noix::core {

class ArgsParser {
public:
    ArgsParser() = default;

    void parse(int argc, char* argv[]);

    bool has(const std::string& key) const;
    std::string get(const std::string& key, const std::string& defaultValue = "") const;
    const std::vector<std::string>& positional() const { return _positional; }

private:
    std::map<std::string, std::string> _named;
    std::vector<std::string> _positional;
};

} // namespace noix::core
