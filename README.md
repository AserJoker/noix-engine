# noix-engine

A lightweight game engine built with C++20, featuring SDL3-based windowing, QuickJS scripting, and DAP-based remote debugging.

## Architecture

```
noix-engine
├── core/          Fundamental utilities
│   ├── ArgsParser     Command-line argument parser (--key=value, -key value, positional)
│   ├── Logger         Logging with std::format, SDL_Log redirect, and pluggable sinks
│   ├── NamespacedId   Value type for namespace:name identifiers
│   └── Sink           Output targets — ConsoleSink (colored) and FileSink (plain)
├── debug/         Remote debug service
│   ├── DebugServer    Session management and command dispatch (HTTP-based)
│   └── DapBridge      DAP debug bridge for QuickJS (stdio protocol)
├── resource/      Resource management
│   └── ResourcePack   Multi-pack resource resolver with priority overlay
├── runtime/       Application lifecycle
│   └── Application    SDL3 window, main event loop, graceful shutdown
└── script/        Scripting runtime
    └── JSEngine      QuickJS-ng wrapper (JSRuntime + JSContext)
```

## Dependencies

All dependencies are included under `third_party/`:

| Library | Purpose | License |
|---------|---------|---------|
| [quickjs-ng](https://github.com/quickjs-ng/quickjs) | JavaScript engine | MIT |
| [cJSON](https://github.com/DaveGamble/cJSON) | JSON parser/serializer | MIT |
| [SDL3](https://github.com/libsdl-org/SDL) | Window, input, event loop | zlib |
| [SDL_net](https://github.com/libsdl-org/SDL_net) | TCP networking (HTTP server sockets) | zlib |

## Build

Requires CMake 3.20+ and a C++20 compiler (MSVC on Windows, Clang/GCC on Linux).

```bash
git clone --recurse-submodules https://github.com/user/noix-engine.git
cd noix-engine

# Windows (MSVC)
cmake -B build
cmake --build build

# Linux (Clang + Ninja)
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

Build outputs go to `dist/`:
- `dist/noix-engine` — main engine executable
- `dist/dap-debug-bridge` — DAP debug bridge (standalone)

## DAP Debug Bridge

The `dap-debug-bridge` is a standalone executable that wraps QuickJS with the Debug Adapter Protocol. It communicates over stdio using DAP's Content-Length framing, making it compatible with VS Code and other DAP clients.

### Quick Start

```bash
# Build
cmake --build build --target dap-debug-bridge

# Run with a JS script
dist/dap-debug-bridge --script path/to/script.js
```

All scripts are evaluated as ES modules (`JS_EVAL_TYPE_MODULE`). Import statements are supported via a custom module loader that compiles imported modules with debug info so breakpoints work across files.

### DAP Protocol Support

| DAP Request | Behavior |
|---|---|
| `initialize` | Capability exchange (supports conditional breakpoints, exception filters, loaded sources) |
| `launch` | Evaluate script as ES module with debug info |
| `disconnect` | Resume execution and clean up |
| `setBreakpoints` | Set/clear breakpoints; line correction for actual code positions |
| `setExceptionBreakpoints` | Enable pause on uncaught or all exceptions |
| `continue` | Resume execution |
| `next` | Step over |
| `stepIn` | Step into |
| `stepOut` | Step out |
| `pause` | Pause on next statement |
| `stackTrace` | Capture call stack |
| `scopes` | Get scope chain for a stack frame |
| `variables` | List variables in a scope or expand object properties |
| `evaluate` | Evaluate expression in frame scope (locals visible) |
| `threads` | Return single "main" thread |
| `loadedSources` | List loaded script files |

### DAP Events

| Event | Trigger |
|---|---|
| `stopped` | Breakpoint hit, step complete, debugger statement, exception |
| `continued` | Execution resumed |
| `terminated` | Script finished |
| `output` | console.log output, stderr |
| `loadedSource` | New script or imported module loaded |

### Architecture

```
DAP Client (VS Code / test script)
       ↕ stdio (Content-Length + JSON)
  DapBridge
    ├── command queue: main thread → script thread (via enqueueAndWait)
    ├── event queue:  script thread → main thread (via pushEvent)
    ├── drainQueue:   script thread calls during pause loop
    └── object expansion: variablesReference → JSValue map
       ↕
  QuickJS Debug API (JS_Debug*)
```

### Known Limitations

- **Module-scope variables in evaluate**: Frame-scoped eval injects frame locals into the global object before evaluation. Module-level variables (`var x = 10` at module top level) are NOT accessible from a function frame's eval — only the function's own locals are visible. This requires scope chain support (planned).
- **Single thread**: QuickJS is single-threaded; DAP always reports one "main" thread.

## Testing

### DAP Protocol Tests

Node.js tests in `tests/dap/` exercise the full DAP protocol by spawning `dap-debug-bridge` as a child process.

```bash
# Run all DAP tests from project root
node tests/dap/dap_client_test.js              # Full protocol test (21 checks)
node tests/dap/dap_multifile_test.js            # Multi-file + ES module breakpoints
node tests/dap/dap_esm_test.js                  # ES module import support
node tests/dap/dap_obj_expansion_test.js        # Object/array property expansion
node tests/dap/dap_eval_obj_expansion_test.js   # Evaluate + expand objects
node tests/dap/dap_breakpoint_while_paused_test.js  # Add/remove breakpoints while paused
node tests/dap/dap_var_test.js                  # Variable inspection
node tests/dap/dap_varindex_test.js             # var/let/const mixed scope variables
node tests/dap/dap_eval_strict2_test.js         # Eval in strict mode (let/const/var)
node tests/dap/dap_tdz_test.js                  # TDZ (temporal dead zone) variable access
```

Test scripts that are loaded by the bridge go in `tests/dap/scripts/`. The shared `DapClient` helper is in `tests/dap/dap_client.js`.

### C Debug API Tests

Low-level QuickJS debug API tests in `tests/debug_test/`:

```bash
# Build on WSL
cd tests/debug_test && make -f Makefile.wsl
./test_debug
```

## NamespacedId

All identifiers in the engine use the `namespace:name` format, inspired by Minecraft's resource location system. The system namespace is `noix`.

```cpp
using namespace noix::core;

// Qualified — explicit namespace
NamespacedId id1("noix", "textures/stone");
// → toString() == "noix:textures/stone"

// Unqualified — defaults to "noix" namespace
NamespacedId id2("textures/stone");
// → ns() == "noix", name() == "textures/stone"

// Parse from string
auto id3 = NamespacedId::parse("mymod:sprites/player");
// → ns() == "mymod", name() == "sprites/player"

auto id4 = NamespacedId::parse("textures/stone");
// → ns() == "noix" (default)
```

`NamespacedId` supports equality comparison and `operator<` (for use as `std::map` keys). Invalid strings (empty, `:name`, `ns:`) throw `std::invalid_argument`.

## Resource Pack

Resources are organized in Minecraft-style directory layout with multi-pack priority overlay.

### Directory Layout

```
<pack-root>/resources/<namespace>/<path>
```

For example:

```
basePath/resources/noix/textures/stone.png      # engine default
modpack/resources/noix/textures/stone.png       # override
modpack/resources/mymod/sprites/player.png      # new resource
```

### Access

Resources are accessed via `NamespacedId`:

```cpp
using namespace noix::core;
using namespace noix::resource;

ResourcePack pack(basePath);
pack.addPack("modpack");  // higher priority than default

// Resolve to filesystem path (searches packs high→low priority)
auto path = pack.resolve(NamespacedId("noix", "textures/stone.png"));
// → points to modpack's version (higher priority)

auto path2 = pack.resolve(NamespacedId("mymod", "sprites/player.png"));
// → points to modpack's version

bool has = pack.exists(NamespacedId("noix", "textures/stone.png"));
// → true
```

### Priority Rules

- The engine's default resource directory (`basePath`) has the **lowest** priority.
- Packs added via `addPack()` have higher priority than the default, in **addition order** (later = higher).
- `resolve()` searches from highest to lowest priority, returning the first match.

## Logging

The engine uses `core::Logger` (singleton) with `std::format_string` for compile-time format checking. Output is dispatched to pluggable sinks:

- **ConsoleSink** — colored level tags on stderr (ANSI escape codes on level label only; message text is plain)
- **FileSink** — plain text to file, no ANSI codes

SDL internal logs are redirected into the Logger via `installSdlRedirect()`.

```cpp
auto& log = core::Logger::instance();
log.addSink(std::make_shared<core::ConsoleSink>());
log.addSink(std::make_shared<core::FileSink>("engine.log"));
log.setLevel(core::LogLevel::Trace);
log.installSdlRedirect();

log.info("server started on port {}", port);
log.warn("session {} expired", sessionId);
log.error("init failed: {}", SDL_GetError());
```

### Log Levels

| Level     | Color   | Usage                        |
|-----------|---------|------------------------------|
| Trace     | Gray    | Verbose internal tracing     |
| Debug     | Cyan    | Debug diagnostics            |
| Info      | White   | Normal operational messages   |
| Warn      | Yellow  | Warning conditions           |
| Error     | Red     | Error conditions             |
| Critical  | Bold Red | Fatal / unrecoverable       |

## HTTP Debug Server

The engine also exposes an HTTP-based debug API on the configured port (default 9900). This is the legacy interface; the DAP bridge is the recommended path for IDE integration.

### Session Lifecycle

1. **Handshake** — discover server capabilities (no session required)
2. **Initialize** — create a session, receive a `sessionId`
3. **Commands** — send commands with `sessionId` (auto-refreshes session timeout)
4. **Disconnect** — explicitly end a session

Sessions expire after 30 seconds of inactivity. Expired sessions must re-initialize.

### Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/debug/handshake` | Server capabilities |
| POST | `/debug/initialize` | Create session |
| POST | `/debug/disconnect` | End session |
| GET | `/debug/status` | Server status |
| GET | `/debug/ping` | Health check |
| POST | `/debug/command` | Execute a debug command |

**eval** — evaluate a JavaScript expression:

```bash
curl -X POST http://localhost:9900/debug/command \
  -d '{"command":"eval","namespace":"debug","arguments":{"sessionId":"<sid>","expr":"1+2+3"}}'
```

**shutdown** — trigger graceful engine shutdown:

```bash
curl -X POST http://localhost:9900/debug/command \
  -d '{"command":"shutdown","namespace":"debug","arguments":{"sessionId":"<sid>"}}'
```

## Project Layout

```
noix-engine/
├── CMakeLists.txt
├── include/
│   ├── core/
│   ├── debug/              Debug server + DAP bridge
│   ├── resource/
│   ├── runtime/
│   └── script/
├── src/
│   ├── main.cpp
│   ├── core/
│   ├── debug/              Debug server + DAP bridge
│   ├── resource/
│   ├── runtime/
│   └── script/
├── tests/
│   ├── dap/                DAP protocol tests (Node.js)
│   │   ├── dap_client.js   Shared DAP client harness
│   │   ├── *_test.js       Test runners
│   │   └── scripts/        JS scripts loaded by the bridge
│   ├── debug_test/         QuickJS debug API C tests
│   ├── integration/        C++ integration tests
│   └── unit/               C++ unit tests
└── third_party/
    ├── quickjs/            QuickJS-ng (enhanced with debug API)
    ├── cJSON/              JSON parser
    ├── SDL/                SDL3
    └── SDL_net/            SDL_net
```

## Conventions

- C++20 standard, MSVC (Windows) / Clang + Ninja (Linux)
- Headers in `include/<module>/`, sources in `src/<module>/`
- Filenames match class names (e.g. `JSEngine.h`)
- camelCase for functions, `_prefix` for private members
- `noix::core`, `noix::debug`, `noix::resource`, `noix::runtime`, `noix::script` namespaces
- Identifiers use `namespace:name` format (e.g. `debug:eval`, `noix:textures/stone`)
