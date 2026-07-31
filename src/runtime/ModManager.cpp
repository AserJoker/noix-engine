#include "runtime/ModManager.h"
#include "core/Logger.h"
#include "core/SemVer.h"
#include "core/Value.h"
#include "runtime/AssetManager.h"
#include "runtime/ConfigManager.h"
#include "script/ScriptEngine.h"
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace noix::runtime {

ModManager::ModManager(std::filesystem::path modsDir, AssetManager& assets)
    : _modsDir(std::move(modsDir)), _assets(assets) {}

ModManager::~ModManager() = default;

void ModManager::discover() {
    _discoveredMods.clear();

    if (!std::filesystem::exists(_modsDir)) {
        std::filesystem::create_directories(_modsDir);
        core::Logger::instance().info("ModManager: created mods directory: {}", _modsDir.string());
    }

    for (const auto& entry : std::filesystem::directory_iterator(_modsDir)) {
        if (!entry.is_directory()) continue;

        auto manifestPath = entry.path() / "manifest.json";
        if (!std::filesystem::exists(manifestPath)) continue;

        // Read manifest.json
        std::ifstream file(manifestPath);
        if (!file.is_open()) continue;

        std::stringstream ss;
        ss << file.rdbuf();
        auto val = core::Value::parse(ss.str());
        if (val.isNull()) {
            core::Logger::instance().warn("ModManager: failed to parse manifest: {}", manifestPath.string());
            continue;
        }

        auto& obj = val.asObject();
        auto nameIt = obj.find("name");
        if (nameIt == obj.end() || !nameIt->second.isString()) {
            core::Logger::instance().warn("ModManager: manifest missing 'name': {}", manifestPath.string());
            continue;
        }

        ModInfo info;
        info.name = nameIt->second.asString();
        info.rootPath = std::filesystem::weakly_canonical(entry.path());

        if (auto it = obj.find("displayName"); it != obj.end() && it->second.isString()) {
            info.displayName = it->second.asString();
        } else {
            info.displayName = info.name;
        }

        if (auto it = obj.find("description"); it != obj.end() && it->second.isString()) {
            info.description = it->second.asString();
        }

        if (auto it = obj.find("version"); it != obj.end() && it->second.isString()) {
            info.version = it->second.asString();
        }

        if (auto it = obj.find("index"); it != obj.end() && it->second.isString()) {
            info.index = it->second.asString();
        }

        if (auto it = obj.find("dependencies"); it != obj.end() && it->second.isObject()) {
            for (auto& [depName, depRange] : it->second.asObject()) {
                if (depRange.isString()) {
                    info.dependencies[depName] = depRange.asString();
                }
            }
        }

        auto iconFile = info.rootPath / "icon.png";
        if (std::filesystem::exists(iconFile)) {
            info.iconPath = iconFile;
        }

        _discoveredMods[info.name] = std::move(info);
    }

    core::Logger::instance().info("ModManager: discovered {} mod(s)", _discoveredMods.size());
}

bool ModManager::hasMod(const std::string& name) const {
    return _discoveredMods.count(name) > 0;
}

const ModManager::ModInfo* ModManager::getMod(const std::string& name) const {
    auto it = _discoveredMods.find(name);
    return it != _discoveredMods.end() ? &it->second : nullptr;
}

std::vector<std::string> ModManager::listMods() const {
    std::vector<std::string> names;
    for (auto& [name, _] : _discoveredMods) {
        names.push_back(name);
    }
    return names;
}

bool ModManager::enable(const std::string& name) {
    if (!hasMod(name)) {
        core::Logger::instance().warn("ModManager: mod not found: {}", name);
        return false;
    }

    // Temporarily set enabled to check dependencies
    _pendingState[name] = true;
    if (!checkDependencies(name)) {
        _pendingState[name] = false;
        core::Logger::instance().warn("ModManager: cannot enable '{}': unsatisfied dependencies", name);
        return false;
    }

    return true;
}

bool ModManager::disable(const std::string& name) {
    if (!hasMod(name)) return false;

    cascadeDisable(name);
    _pendingState[name] = false;
    return true;
}

bool ModManager::isEnabled(const std::string& name) const {
    auto it = _pendingState.find(name);
    return it != _pendingState.end() && it->second;
}

bool ModManager::commit() {
    // Validate all enabled mods
    for (auto& [name, enabled] : _pendingState) {
        if (!enabled) continue;
        if (!hasMod(name)) {
            core::Logger::instance().warn("ModManager: enabled mod '{}' no longer exists", name);
            return false;
        }
        if (!checkDependencies(name)) {
            core::Logger::instance().warn("ModManager: dependency check failed for '{}'", name);
            return false;
        }
    }

    if (hasCircularDependency()) {
        core::Logger::instance().warn("ModManager: circular dependency detected");
        return false;
    }

    // Persist
    _persistedState = _pendingState;

    // Apply to asset manager
    applyToAssetManager();

    // Trigger subsystem reset callback
    if (_onCommit) _onCommit();

    core::Logger::instance().info("ModManager: changes committed");
    return true;
}

void ModManager::rollback() {
    _pendingState = _persistedState;
    core::Logger::instance().info("ModManager: changes rolled back");
}

void ModManager::loadState(const ConfigManager& config) {
    _persistedState.clear();
    _pendingState.clear();

    auto val = config.get(core::NamespacedId("noix", "mod-state"));
    if (val.isNull() || !val.isObject()) return;

    for (auto& [name, enabled] : val.asObject()) {
        if (enabled.isBool()) {
            _persistedState[name] = enabled.asBool();
        }
    }

    // Validate: remove entries for mods that no longer exist
    std::vector<std::string> toRemove;
    for (auto& [name, enabled] : _persistedState) {
        if (!hasMod(name)) {
            core::Logger::instance().warn("ModManager: previously enabled mod '{}' not found on disk", name);
            toRemove.push_back(name);
        }
    }
    for (auto& name : toRemove) {
        _persistedState.erase(name);
    }

    // Validate dependencies for persisted enabled mods
    for (auto& [name, enabled] : _persistedState) {
        if (enabled && !checkDependenciesForPersisted(name)) {
            core::Logger::instance().warn("ModManager: disabling '{}' due to unsatisfied dependencies", name);
            _persistedState[name] = false;
        }
    }

    _pendingState = _persistedState;
}

void ModManager::saveState(ConfigManager& config) const {
    core::Value state = core::Value::object();
    for (auto& [name, enabled] : _persistedState) {
        state.asObject()[name] = core::Value(enabled);
    }
    config.set(core::NamespacedId("noix", "mod-state"), std::move(state));
}

std::vector<std::string> ModManager::enabledModsInLoadOrder() const {
    // Topological sort of enabled mods (dependencies first)
    std::vector<std::string> result;
    std::map<std::string, bool> visited;

    // Only consider enabled mods
    std::set<std::string> enabledSet;
    for (auto& [name, en] : _pendingState) {
        if (en) enabledSet.insert(name);
    }

    std::function<void(const std::string&)> visit = [&](const std::string& name) {
        if (visited[name]) return;
        visited[name] = true;

        auto it = _discoveredMods.find(name);
        if (it == _discoveredMods.end()) return;

        for (auto& [dep, _] : it->second.dependencies) {
            if (enabledSet.count(dep)) {
                visit(dep);
            }
        }

        result.push_back(name);
    };

    for (auto& name : enabledSet) {
        visit(name);
    }

    return result;
}

std::optional<std::filesystem::path> ModManager::resolveModIndex(const std::string& name) const {
    auto it = _discoveredMods.find(name);
    if (it == _discoveredMods.end()) return std::nullopt;
    if (it->second.index.empty()) return std::nullopt;
    return it->second.rootPath / it->second.index;
}

void ModManager::setOnCommit(CommitCallback cb) {
    _onCommit = std::move(cb);
}

void ModManager::applyToAssetManager() {
    // Remove all existing mod packs
    auto packs = _assets.listPacks();
    for (auto& pack : packs) {
        _assets.removePack(pack);
    }

    // Add enabled mod directories as packs (in load order = dependency order)
    auto enabled = enabledModsInLoadOrder();
    for (auto& name : enabled) {
        auto it = _discoveredMods.find(name);
        if (it != _discoveredMods.end()) {
            _assets.addPack(it->second.rootPath);
        }
    }
}

void ModManager::loadMods(noix::script::ScriptEngine& engine) {
    auto enabled = enabledModsInLoadOrder();

    // Load each enabled mod's index script in dependency order.
    // The absolute path serves as the QuickJS module cache key, so
    // import "modname" from other modules resolves to the same cached module.
    for (auto& name : enabled) {
        auto indexPath = resolveModIndex(name);
        if (indexPath) {
            engine.loadScriptAsync(indexPath->string());
        }
    }

    // Load the engine entry script
    engine.loadScriptAsync(engine.scriptsPath() + "/entry.js");
}

bool ModManager::checkDependencies(const std::string& name) const {
    auto it = _discoveredMods.find(name);
    if (it == _discoveredMods.end()) return false;

    for (auto& [depName, depRange] : it->second.dependencies) {
        auto depIt = _discoveredMods.find(depName);
        if (depIt == _discoveredMods.end()) {
            core::Logger::instance().warn("ModManager: '{}' depends on '{}' which is not installed", name, depName);
            return false;
        }

        // Check dependency is enabled in pending state
        auto stateIt = _pendingState.find(depName);
        if (stateIt == _pendingState.end() || !stateIt->second) {
            core::Logger::instance().warn("ModManager: '{}' depends on '{}' which is not enabled", name, depName);
            return false;
        }

        // Check version compatibility
        if (!depRange.empty()) {
            auto depVer = core::SemVer::parse(depIt->second.version);
            auto range = core::VersionRange::parse(depRange);
            if (!range.satisfies(depVer)) {
                core::Logger::instance().warn("ModManager: '{}' requires '{}' {}, but found {}",
                    name, depName, depRange, depIt->second.version);
                return false;
            }
        }
    }

    return true;
}

bool ModManager::checkDependenciesForPersisted(const std::string& name) const {
    auto it = _discoveredMods.find(name);
    if (it == _discoveredMods.end()) return false;

    for (auto& [depName, depRange] : it->second.dependencies) {
        auto depIt = _discoveredMods.find(depName);
        if (depIt == _discoveredMods.end()) return false;

        auto stateIt = _persistedState.find(depName);
        if (stateIt == _persistedState.end() || !stateIt->second) return false;

        if (!depRange.empty()) {
            auto depVer = core::SemVer::parse(depIt->second.version);
            auto range = core::VersionRange::parse(depRange);
            if (!range.satisfies(depVer)) return false;
        }
    }

    return true;
}

void ModManager::cascadeDisable(const std::string& name) {
    // Find all mods that depend on 'name' and are currently enabled
    for (auto& [modName, info] : _discoveredMods) {
        if (info.dependencies.count(name) == 0) continue;

        auto it = _pendingState.find(modName);
        if (it != _pendingState.end() && it->second) {
            cascadeDisable(modName);
            _pendingState[modName] = false;
            core::Logger::instance().debug("ModManager: cascading disable '{}'", modName);
        }
    }
}

bool ModManager::hasCircularDependency() const {
    std::map<std::string, int> visited; // 0=unvisited, 1=in-progress, 2=done

    for (auto& [name, enabled] : _pendingState) {
        if (!enabled) continue;
        if (detectCycle(name, visited)) return true;
    }
    return false;
}

bool ModManager::detectCycle(const std::string& node,
                             std::map<std::string, int>& visited) const {
    if (visited[node] == 1) return true;  // cycle detected
    if (visited[node] == 2) return false; // already processed

    visited[node] = 1; // in-progress

    auto it = _discoveredMods.find(node);
    if (it != _discoveredMods.end()) {
        for (auto& [dep, _] : it->second.dependencies) {
            auto stateIt = _pendingState.find(dep);
            if (stateIt != _pendingState.end() && stateIt->second) {
                if (detectCycle(dep, visited)) return true;
            }
        }
    }

    visited[node] = 2; // done
    return false;
}

} // namespace noix::runtime
