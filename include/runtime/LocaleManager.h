#pragma once

/*
 * LocaleManager — Internationalization (i18n) support for the engine.
 *
 * Loads language files from ResourcePack, respecting pack overlay priority.
 * Language keys inherit the namespace of their source file:
 *   key "system.window.title" in "noix:i18n/en_US.lang" → "noix:system.window.title"
 *
 * Owned by Application, accessed via Application::instance().localeManager().
 */

#include "core/NamespacedId.h"

#include <filesystem>
#include <map>
#include <set>
#include <string>

namespace noix::runtime {

class AssetManager;

class LocaleManager {
public:
    explicit LocaleManager(AssetManager* assets);

    /// Set the AssetManager used to resolve .lang files.
    void setAssetManager(AssetManager* assets);

    /// Set the current locale (e.g., "en_US") and reload translations.
    void setLang(const std::string& lang);

    /// Get the current locale string.
    const std::string& lang() const { return _lang; }

    /// Add a namespace to the whitelist. If non-empty, only these namespaces
    /// are loaded during setLang(). Triggers a reload if a locale is active.
    void addNamespace(const std::string& ns);

    /// Remove a namespace from the whitelist. Triggers a reload if a locale is active.
    void removeNamespace(const std::string& ns);

    /// Get the current namespace whitelist (empty = load all namespaces).
    const std::set<std::string>& namespaces() const { return _namespaces; }

    /// Reset all state — clear translations, locale, and namespace whitelist.
    void reset();

    /// Look up a translation by fully-qualified key (e.g., "noix:system.window.title").
    /// Returns defaultValue if the key is not found.
    std::string i18n(const std::string& key, const std::string& defaultValue = "") const;

private:
    /// Load all .lang files for the current locale from all resource packs.
    void loadTranslations();

    /// Parse a single .lang file. Keys are prefixed with the given namespace.
    void parseLangFile(const std::filesystem::path& path, const std::string& ns);

    AssetManager* _assets = nullptr;
    std::string _lang;
    std::set<std::string> _namespaces;
    std::map<core::NamespacedId, std::string> _translations;
};

} // namespace noix::runtime
