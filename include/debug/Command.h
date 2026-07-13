#pragma once

#include <string>

namespace noix::debug {

class Command {
public:
    virtual ~Command() = default;
    virtual std::string execute(const std::string& arguments) = 0;
};

} // namespace noix::debug
