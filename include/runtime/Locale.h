#pragma once

/*
 * Locale — Internationalization (i18n) support for the engine.
 *
 * Loads language files from ResourcePack, respecting pack overlay priority.
 * Language keys inherit the namespace of their source file:
 *   key "system.window.title" in "noix:i18n/en_US.lang" → "noix:system.window.title"
 *
 * Usage:
 *   Locale::instance().setResourcePack(&pack);
 *   Locale::instance().setLang("en_US");
 *   std::string title = Locale::instance().i18n("noix:system.window.title", "fallback");
 */

#include "core/NamespacedId.h"

#include <filesystem>
#include <map>
#include <string>

namespace noix::resource { class ResourcePack; }

namespace noix::runtime {

class Locale {
public:
    static Locale& instance();

    /// Set the ResourcePack used to resolve .lang files.
    void setResourcePack(resource::ResourcePack* pack);

    /// Set the current locale (e.g., "en_US") and reload translations.
    void setLang(const std::string& lang);

    /// Get the current locale string.
    const std::string& lang() const { return _lang; }

    /// Reset all state — clear translations and locale.
    void reset();

    /// Look up a translation by fully-qualified key (e.g., "noix:system.window.title").
    /// Returns defaultValue if the key is not found.
    std::string i18n(const std::string& key, const std::string& defaultValue = "") const;

private:
    Locale() = default;

    /// Load all .lang files for the current locale from all resource packs.
    void loadTranslations();

    /// Parse a single .lang file. Keys are prefixed with the given namespace.
    void parseLangFile(const std::filesystem::path& path, const std::string& ns);

    resource::ResourcePack* _pack = nullptr;
    std::string _lang;
    std::map<core::NamespacedId, std::string> _translations;
};

} // namespace noix::runtime
