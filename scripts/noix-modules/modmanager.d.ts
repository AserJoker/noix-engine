/**
 * noix:modmanager — Mod management module for script-driven mod lifecycle.
 *
 * Mods are discovered from the mods directory. Enable/disable operations
 * are temporary until commit() is called. Commit validates dependencies
 * and triggers a system reset.
 */

export interface ModInfo {
  name: string;
  displayName: string;
  description: string;
  version: string;
  enabled: boolean;
}

export function listMods(): ModInfo[];
export function enable(name: string): boolean;
export function disable(name: string): boolean;
export function commit(): boolean;
export function rollback(): void;
