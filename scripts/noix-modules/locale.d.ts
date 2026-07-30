/**
 * Look up a translation by fully-qualified key.
 * Keys follow the "namespace:key" format, e.g. "noix:system.window.title".
 * The namespace is inherited from the .lang file's location in the resource pack.
 *
 * @param key - Fully-qualified translation key (e.g. "noix:system.window.title")
 * @param defaultValue - Fallback value returned when the key is not found. Defaults to "".
 * @returns The translated string, or defaultValue if not found.
 */
export function i18n(key: string, defaultValue?: string): string;

/**
 * Set the current locale and reload translations from all resource packs.
 * Language files are resolved as "namespace:i18n/{lang}.lang" through ResourcePack.
 *
 * @param lang - Locale identifier, e.g. "en_US", "zh_CN"
 */
export function setLang(lang: string): void;

/**
 * Get the current locale identifier.
 *
 * @returns The current locale string, e.g. "en_US"
 */
export function getLang(): string;

/**
 * Reset all locale state — clears translations and the current locale string.
 */
export function reset(): void;
