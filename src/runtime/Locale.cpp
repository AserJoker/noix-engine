/*
 * Locale — Internationalization (i18n) support for the engine.
 *
 * Loads .lang files from ResourcePack, respecting pack overlay priority.
 * Higher-priority pack entries override lower-priority ones.
 */

#include "runtime/Locale.h"
#include "resource/ResourcePack.h"
#include "core/Logger.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace noix::runtime {

Locale& Locale::instance() {
    static Locale locale;
    return locale;
}

void Locale::setResourcePack(resource::ResourcePack* pack) {
    _pack = pack;
}

void Locale::setLang(const std::string& lang) {
    _lang = lang;
    loadTranslations();
}

void Locale::reset() {
    _lang.clear();
    _translations.clear();
}

std::string Locale::i18n(const std::string& key, const std::string& defaultValue) const {
    auto id = core::NamespacedId::parse(key);
    auto it = _translations.find(id);
    if (it != _translations.end()) {
        return it->second;
    }
    return defaultValue;
}

void Locale::loadTranslations() {
    _translations.clear();

    if (!_pack || _lang.empty()) return;

    /* Build the resource name for the lang file: "i18n/<locale>.lang" */
    std::string langResource = "i18n/" + _lang + ".lang";

    /* Iterate all packs in low→high priority order.
       Parse each pack's lang file; higher-priority entries overwrite
       lower-priority ones. */
    auto packs = _pack->listPacks();
    auto defaultPath = _pack->defaultPath();

    /* Collect all paths that contain the lang file, in low→high priority */
    std::vector<std::pair<std::filesystem::path, std::string>> paths;

    /* Default path (lowest priority) */
    auto relPath = std::filesystem::path("assets") / langResource;
    for (const auto& entry : std::filesystem::directory_iterator(defaultPath)) {
        if (!entry.is_directory()) continue;
        auto candidate = entry.path() / relPath;
        if (std::filesystem::exists(candidate)) {
            paths.emplace_back(candidate, entry.path().filename().string());
        }
    }

    /* User packs (in order, lowest first) */
    for (const auto& packRoot : packs) {
        for (const auto& entry : std::filesystem::directory_iterator(packRoot)) {
            if (!entry.is_directory()) continue;
            auto candidate = entry.path() / relPath;
            if (std::filesystem::exists(candidate)) {
                paths.emplace_back(candidate, entry.path().filename().string());
            }
        }
    }

    /* Parse in order — later entries (higher priority) overwrite earlier ones */
    for (auto& [path, ns] : paths) {
        parseLangFile(path, ns);
    }

    core::Logger::instance().info("Locale: loaded {} translations for '{}'",
                                   _translations.size(), _lang);
}

void Locale::parseLangFile(const std::filesystem::path& path, const std::string& ns) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        /* Skip empty lines and comments */
        if (line.empty() || line[0] == '#') continue;

        /* Find separator " = " */
        auto sepPos = line.find(" = ");
        if (sepPos == std::string::npos) continue;

        std::string key = line.substr(0, sepPos);
        std::string value = line.substr(sepPos + 3);

        /* Trim whitespace from key */
        size_t start = key.find_first_not_of(" \t");
        size_t end = key.find_last_not_of(" \t");
        if (start == std::string::npos) continue;
        key = key.substr(start, end - start + 1);

        /* Strip surrounding quotes from value */
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }

        /* Namespace the key */
        auto id = core::NamespacedId(ns, key);
        _translations[id] = value;
    }
}

} // namespace noix::runtime
