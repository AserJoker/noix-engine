#pragma once

#include "debug/Command.h"

namespace noix::core { class ConfigManager; }

namespace noix::debug {

class ConfigGetCommand : public Command {
public:
    explicit ConfigGetCommand(core::ConfigManager& configManager);
    std::string execute(const std::string& arguments) override;

private:
    core::ConfigManager& _configManager;
};

class ConfigSetCommand : public Command {
public:
    explicit ConfigSetCommand(core::ConfigManager& configManager);
    std::string execute(const std::string& arguments) override;

private:
    core::ConfigManager& _configManager;
};

class ConfigRemoveCommand : public Command {
public:
    explicit ConfigRemoveCommand(core::ConfigManager& configManager);
    std::string execute(const std::string& arguments) override;

private:
    core::ConfigManager& _configManager;
};

class ConfigSaveCommand : public Command {
public:
    explicit ConfigSaveCommand(core::ConfigManager& configManager);
    std::string execute(const std::string& arguments) override;

private:
    core::ConfigManager& _configManager;
};

class ConfigListCommand : public Command {
public:
    explicit ConfigListCommand(core::ConfigManager& configManager);
    std::string execute(const std::string& arguments) override;

private:
    core::ConfigManager& _configManager;
};

} // namespace noix::debug
