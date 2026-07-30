/**
 * Get an entire config group as a typed JS object.
 * If the config ID does not exist, the default value is returned and
 * persisted to disk automatically.
 *
 * @param id - Namespaced config ID, e.g. "noix:application"
 * @param defaultValue - Fallback value used (and persisted) when the config is missing
 * @returns The config object typed as T
 */
export function get<T extends Record<string, unknown>>(
  id: string,
  defaultValue: T,
): T;

/**
 * Set an entire config group from a JS object. Auto-saves to disk.
 *
 * @param id - Namespaced config ID, e.g. "noix:application"
 * @param value - The config object to store
 */
export function set(id: string, value: Record<string, unknown>): void;

/**
 * Check if a config group exists.
 *
 * @param id - Namespaced config ID
 * @returns true if the config exists
 */
export function has(id: string): boolean;

/**
 * Remove an entire config group (deletes from memory and disk).
 *
 * @param id - Namespaced config ID
 * @returns true if the config was found and removed
 */
export function remove(id: string): boolean;

/**
 * List all loaded config IDs.
 *
 * @returns Array of namespaced config IDs, e.g. ["noix:application"]
 */
export function list(): string[];
