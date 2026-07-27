# DAP Protocol Reference (Debug Adapter Protocol)

Source: https://microsoft.github.io/debug-adapter-protocol/specification

## Initialize Response Capabilities

Stepping-related capabilities (in `Capabilities` object):

| Capability | Type | Description |
|---|---|---|
| `supportsStepBack` | boolean | Supports `stepBack` and `reverseContinue` requests |
| `supportsStepInTargetsRequest` | boolean | Supports `stepInTargets` request |
| `supportsSteppingGranularity` | boolean | Supports `granularity` argument for stepping requests |
| `supportsSingleThreadExecutionRequests` | boolean | Supports `singleThread` on execution requests |

**Note:** `next` (step over), `stepIn`, `stepOut` do NOT require any capability flag — they are always available.

### Other important capabilities

| Capability | Type | Description |
|---|---|---|
| `supportsConfigurationDoneRequest` | boolean | Supports `configurationDone` request |
| `supportsConditionalBreakpoints` | boolean | Supports conditional breakpoints |
| `supportsExceptionInfoRequest` | boolean | Supports `exceptionInfo` request |
| `supportsSetVariable` | boolean | Supports `setVariable` request |
| `supportsLoadedSourcesRequest` | boolean | Supports `loadedSources` request |
| `supportsHitConditionalBreakpoints` | boolean | Supports hit count conditional breakpoints |
| `supportsFunctionBreakpoints` | boolean | Supports function breakpoints |
| `supportsEvaluateForHovers` | boolean | Supports evaluate request for hover |

## Stopped Event

```json
{
  "type": "event",
  "event": "stopped",
  "body": {
    "reason": "step|breakpoint|exception|pause|entry|goto|function breakpoint|data breakpoint|instruction breakpoint",
    "description": "Optional. Full reason shown in UI (e.g. 'Paused on exception')",
    "threadId": 1,
    "preserveFocusHint": false,
    "text": "Optional. Additional info (e.g. exception name)",
    "allThreadsStopped": true,
    "hitBreakpointIds": [1]
  }
}
```

**Required:** `reason` (the only non-optional field)
**Important:** `threadId` is technically optional but practically required by most clients.

Valid `reason` values: `step`, `breakpoint`, `exception`, `pause`, `entry`, `goto`, `function breakpoint`, `data breakpoint`, `instruction breakpoint`

## Next Request (Step Over)

```json
// Request
{
  "command": "next",
  "arguments": {
    "threadId": 1,
    "singleThread": false,
    "granularity": "statement|line|instruction"
  }
}

// Response — acknowledgement only, no body required
{
  "type": "response",
  "command": "next",
  "success": true
}
```

**After response:** The adapter MUST send a `stopped` event with `reason: "step"` after the step completes.

## StepIn / StepOut Requests

Same format as `next` but with `command: "stepIn"` or `command: "stepOut"`.

## Continue Request

```json
{
  "command": "continue",
  "arguments": {
    "threadId": 1,
    "singleThread": false
  }
}

// Response
{
  "type": "response",
  "command": "continue",
  "body": {
    "allThreadsContinued": true
  }
}
```

## Attach Request

```json
{
  "command": "attach",
  "arguments": {
    "__restart": "Optional. Data from previous restarted session."
  }
}
```

**Note:** The spec defines NO standard attach arguments. All actual arguments (port, host, processId, etc.) are implementation-specific.

## Session Lifecycle (Attach Mode)

```
1. Client → initialize request
2. Adapter → initialize response (with Capabilities)
3. Adapter → initialized event
4. Client → setBreakpoints, setExceptionBreakpoints, etc.
5. Client → configurationDone request (if supportsConfigurationDoneRequest)
6. Client → attach request
7. Adapter → attach response
8. Adapter → stopped event (reason: "entry" or other)
9. Client → next/stepIn/stepOut/continue (with threadId from stopped event)
10. Adapter → response + stopped event (reason: "step")
```

**Critical:** The adapter must send the `initialized` event AFTER the `initialize` response and BEFORE the client sends `attach` or configuration requests.

## Source Request

```json
// Request
{
  "command": "source",
  "arguments": {
    "source": { "path": "/path/to/file.js" },
    "sourceReference": 0
  }
}

// Response
{
  "type": "response",
  "command": "source",
  "body": {
    "content": "file contents here",
    "mimeType": "text/javascript"
  }
}
```

**Note:** If `sourceReference` is 0 or missing, the client reads the file directly from disk. Non-zero `sourceReference` requires the adapter to provide content via this request.

## StackTrace Request

```json
{
  "command": "stackTrace",
  "arguments": {
    "threadId": 1,
    "startFrame": 0,
    "levels": 20
  }
}

// Response
{
  "body": {
    "stackFrames": [
      {
        "id": 0,
        "name": "functionName",
        "source": { "name": "file.js", "path": "/abs/path/file.js", "sourceReference": 0 },
        "line": 10,
        "column": 5
      }
    ],
    "totalFrames": 5
  }
}
```

## Disconnect Request

```json
{
  "command": "disconnect",
  "arguments": {
    "restart": false,
    "terminateDebuggee": true
  }
}
```

The adapter should respond immediately, then clean up.
