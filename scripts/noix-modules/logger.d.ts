export function trace(...args: unknown[]): void;
export function debug(...args: unknown[]): void;
export function info(...args: unknown[]): void;
export function warn(...args: unknown[]): void;
export function error(...args: unknown[]): void;
export function critical(...args: unknown[]): void;
export function setLevel(level: "trace" | "debug" | "info" | "warn" | "error" | "critical"): void;
export function getLevel(): string;
