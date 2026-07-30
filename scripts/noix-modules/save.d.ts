/**
 * Write data to a save file. Creates directories recursively.
 *
 * @param slot - Save slot name, e.g. "world1"
 * @param path - Namespaced file path, e.g. "noix:player/inventory.dat"
 * @param data - The data to write (string content)
 * @returns true on success
 */
export function save(slot: string, path: string, data: string): boolean;

/**
 * Read data from a save file.
 *
 * @param slot - Save slot name
 * @param path - Namespaced file path
 * @returns The file content as a string, or null if not found
 */
export function load(slot: string, path: string): string | null;

/**
 * Check if a save file exists.
 *
 * @param slot - Save slot name
 * @param path - Namespaced file path
 * @returns true if the file exists
 */
export function exists(slot: string, path: string): boolean;

/**
 * Remove a single save file.
 *
 * @param slot - Save slot name
 * @param path - Namespaced file path
 * @returns true if the file was found and removed
 */
export function remove(slot: string, path: string): boolean;

/**
 * Delete an entire save slot (all files under saves/<slot>).
 *
 * @param slot - Save slot name
 * @returns true if the slot existed and was deleted
 */
export function deleteSlot(slot: string): boolean;

/**
 * List all save slot names.
 *
 * @returns Array of slot names, e.g. ["world1", "world2"]
 */
export function listSlots(): string[];
