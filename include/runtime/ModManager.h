#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace noix::script { class ScriptEngine; }

namespace noix::runtime {

class AssetManager;
class ConfigManager;

class ModManager {
public:
    explicit ModManager(std::filesystem::path modsDir, AssetManager& assets);
    ~ModManager();

    ModManager(const ModManager&) = delete;
    ModManager& operator=(const ModManager&) = delete;

    /// Mod info parsed from manifest.json
    struct ModInfo {
        std::string name;                // also namespace
        std::string displayName;         // human-readable name (optional)
        std::string description;         // mod description (optional)
        std::filesystem::path iconPath;  // rootPath / "icon.png" (may not exist)
        std::string version;             // semver string
        std::map<std::string, std::string> dependencies; // name -> version range
        std::string index;               // script entry relative to mod root (optional)
        std::filesystem::path rootPath;  // absolute path to mod directory
    };

    /// Scan modsDir for directories containing manifest.json
    void discover();

    /// Query
    bool hasMod(const std::string& name) const;
    const ModInfo* getMod(const std::string& name) const;
    std::vector<std::string> listMods() const;

    /// Enable/disable (temporary state)
    bool enable(const std::string& name);
    bool disable(const std::string& name);
    bool isEnabled(const std::string& name) const;

    /// Commit: validate all, persist to config, trigger subsystem reset
    bool commit();
    /// Rollback: revert pending changes to persisted state
    void rollback();

    /// Load enabled state from config (called at startup)
    void loadState(const ConfigManager& config);
    /// Save current persisted state to config
    void saveState(ConfigManager& config) const;

    /// Get ordered list of enabled mods (dependency-sorted, for module loader)
    std::vector<std::string> enabledModsInLoadOrder() const;

    /// Resolve mod name to its index script path
    std::optional<std::filesystem::path> resolveModIndex(const std::string& name) const;

    /// Set callback triggered on commit (for subsystem reset)
    using CommitCallback = std::function<void()>;
    void setOnCommit(CommitCallback cb);

    /// Apply enabled mod directories as asset packs
    void applyToAssetManager();

    /// Load all enabled mods' index scripts and entry.js via ScriptEngine
    void loadMods(noix::script::ScriptEngine& engine);

private:
    bool checkDependencies(const std::string& name) const;
    bool checkDependenciesForPersisted(const std::string& name) const;
    void cascadeDisable(const std::string& name);
    bool hasCircularDependency() const;
    bool detectCycle(const std::string& node,
                     std::map<std::string, int>& visited) const;

    std::filesystem::path _modsDir;
    AssetManager& _assets;
    CommitCallback _onCommit;

    /// All discovered mods: name -> ModInfo
    std::map<std::string, ModInfo> _discoveredMods;

    /// Current persisted state: name -> enabled
    std::map<std::string, bool> _persistedState;

    /// Pending state: name -> enabled (copy of _persistedState with staged changes)
    std::map<std::string, bool> _pendingState;
};

} // namespace noix::runtime
