/**
 * noix:eventbus — Event bus module for script-driven event dispatch.
 *
 * Events are identified by NamespacedId strings (e.g., "player:healthChanged").
 * Scripts register listeners via addEventListener and emit events via emit.
 * All event dispatch goes through the SDL event queue (async).
 * Listeners are automatically cleaned up on engine shutdown.
 */

/**
 * Register a listener for an event.
 * @param eventName The event name in NamespacedId format (e.g., "player:healthChanged")
 * @param callback Function called when the event fires, receives the event data object
 * @returns A handle that can be passed to removeEventListener
 */
export function addEventListener(
  eventName: string,
  callback: (data: Record<string, unknown>) => void
): number;

/**
 * Remove a previously registered listener.
 * @param handle The handle returned by addEventListener
 */
export function removeEventListener(handle: number): void;

/**
 * Emit an event asynchronously (goes through SDL event queue).
 * @param eventName The event name in NamespacedId format
 * @param data Optional data payload (object)
 */
export function emit(eventName: string, data?: Record<string, unknown>): void;
