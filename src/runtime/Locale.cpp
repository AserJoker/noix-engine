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

void Locale::addNamespace(const std::string& ns) {
    _namespaces.insert(ns);
    if (!_lang.empty()) loadTranslations();
}

void Locale::removeNamespace(const std::string& ns) {
    _namespaces.erase(ns);
    if (!_lang.empty()) loadTranslations();
}

void Locale::reset() {
    _lang.clear();
    _namespaces.clear();
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

    /* Build the relative path inside a namespace: "i18n/<locale>.lang" */
    std::string langResource = "i18n/" + _lang + ".lang";

    auto packs = _pack->listPacks();
    auto defaultPath = _pack->defaultPath();

    /* Collect all pack roots in low→high priority order.
       defaultPath is lowest, then user packs in their listed order. */
    std::vector<std::filesystem::path> roots;
    roots.push_back(defaultPath);
    for (const auto& p : packs) roots.push_back(p);

    /* Build namespace list: use explicit whitelist if non-empty,
       otherwise auto-discover by scanning assets/ in all pack roots. */
    std::vector<std::string> nsList;
    if (!_namespaces.empty()) {
        nsList.assign(_namespaces.begin(), _namespaces.end());
    } else {
        std::set<std::string> discovered;
        for (const auto& root : roots) {
            auto assetsDir = root / "assets";
            if (!std::filesystem::exists(assetsDir)) continue;
            for (const auto& nsDir : std::filesystem::directory_iterator(assetsDir)) {
                if (nsDir.is_directory()) {
                    discovered.insert(nsDir.path().filename().string());
                }
            }
        }
        nsList.assign(discovered.begin(), discovered.end());
    }

    /* For each namespace, walk pack roots in low→high priority order.
       Higher-priority packs overwrite lower-priority entries. */
    for (const auto& ns : nsList) {
        for (const auto& root : roots) {
            auto candidate = root / "assets" / ns / langResource;
            if (std::filesystem::exists(candidate)) {
                parseLangFile(candidate, ns);
            }
        }
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
