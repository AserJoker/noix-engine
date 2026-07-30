/**
 * Request object passed to a script-registered command handler.
 * This is the parsed JSON body of the HTTP POST request.
 */
export interface DebugRequest {
    [key: string]: unknown;
}

/**
 * Response object returned from a script-registered command handler.
 * Will be serialized as the HTTP response body.
 */
export interface DebugResponse {
    [key: string]: unknown;
}

/**
 * Register a script-facing REST API endpoint on the DebugServer.
 *
 * @param name - Hierarchical endpoint name, e.g. "script/echo".
 *               Maps to POST /api/v{version}/{name}
 * @param version - API version string, e.g. "v1".
 * @param handler - Callback invoked when the endpoint receives a request.
 *                  Receives the parsed request object, must return a response object.
 */
export function registerCommand(
    name: string,
    version: string,
    handler: (request: DebugRequest) => DebugResponse
): void;
