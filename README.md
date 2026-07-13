# noix-engine

A lightweight game engine built with C++20, featuring SDL3-based windowing, QuickJS scripting, and an HTTP-based remote debug protocol.

## Architecture

```
noix-engine
├── core/          Fundamental utilities
│   ├── ArgsParser     Command-line argument parser (--key=value, -key value, positional)
│   ├── Logger         Logging with std::format, SDL_Log redirect, and pluggable sinks
│   ├── NamespacedId   Value type for namespace:name identifiers
│   └── Sink           Output targets — ConsoleSink (colored) and FileSink (plain)
├── debug/         Remote debug service
│   ├── HttpServer     Lightweight HTTP server built on SDL3_net (non-blocking, per-request close)
│   ├── DebugServer    Session management and command dispatch
│   ├── Command        Abstract base class for debug commands
│   ├── EvalCommand    Evaluate JavaScript expressions via JSEngine
│   └── ShutdownCommand  Trigger graceful engine shutdown via SDL event
├── resource/      Resource management
│   └── ResourcePack   Multi-pack resource resolver with priority overlay
├── runtime/       Application lifecycle
│   └── Application    SDL3 window, main event loop, graceful shutdown
└── script/        Scripting runtime
    └── JSEngine      QuickJS-ng wrapper (JSRuntime + JSContext)
```

## Dependencies

All dependencies are included as git submodules under `third_party/`:

| Library | Purpose | License |
|---------|---------|---------|
| [SDL3](https://github.com/libsdl-org/SDL) | Window, input, event loop | zlib |
| [SDL_net](https://github.com/libsdl-org/SDL_net) | TCP networking (HTTP server sockets) | zlib |
| [quickjs-ng](https://github.com/quickjs-ng/quickjs) | JavaScript engine | MIT |
| [cJSON](https://github.com/DaveGamble/cJSON) | JSON parser/serializer | MIT |

## Build

Requires CMake 3.20+, Clang (recommended), and Ninja.

```bash
git clone --recurse-submodules https://github.com/user/noix-engine.git
cd noix-engine

cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

## Run

```bash
./build/noix-engine              # default debug port 9900
./build/noix-engine --debug-port=8800
```

The engine opens an SDL3 window and starts an HTTP debug server. Close the window or send a shutdown command to exit.

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

## Debug Protocol

The debug server exposes an HTTP API on the configured port. All request/response bodies use JSON.

### Session Lifecycle

1. **Handshake** — discover server capabilities (no session required)
2. **Initialize** — create a session, receive a `sessionId`
3. **Commands** — send commands with `sessionId` (auto-refreshes session timeout)
4. **Disconnect** — explicitly end a session

Sessions expire after 30 seconds of inactivity. Expired sessions must re-initialize.

### Command System

Debug commands are registered with `NamespacedId` keys using the `namespace:name` format:

```cpp
_debugServer->registerCommand(
    core::NamespacedId("debug", "eval"),
    std::make_unique<debug::EvalCommand>(*_jsEngine));
```

Commands are implemented as classes inheriting from `debug::Command`:

```cpp
class Command {
public:
    virtual ~Command() = default;
    virtual std::string execute(const std::string& arguments) = 0;
};
```

### Endpoints

#### GET /debug/handshake

```json
{"command":"handshake","namespace":"debug","result":{"name":"noix-engine","version":"0.1.0","protocol":"1.0"}}
```

#### POST /debug/initialize

```bash
curl -X POST http://localhost:9900/debug/initialize \
  -H "Content-Type: application/json" \
  -d '{"arguments":{"clientName":"my-client","clientVersion":"1.0"}}'
```

```json
{"command":"initialize","namespace":"debug","result":{"sessionId":"a1b2c3d4...","timeoutSeconds":30,"commands":["debug:eval","debug:shutdown"]}}
```

#### POST /debug/disconnect

```bash
curl -X POST http://localhost:9900/debug/disconnect \
  -d '{"arguments":{"sessionId":"<sessionId>"}}'
```

#### GET /debug/status

```json
{"command":"status","namespace":"debug","result":{"uptime":12.5,"sessions":1}}
```

#### GET /debug/ping

```json
{"pong":true}
```

#### POST /debug/command

All commands require a valid `sessionId`.

**eval** — evaluate a JavaScript expression:

```bash
curl -X POST http://localhost:9900/debug/command \
  -d '{"command":"eval","namespace":"debug","arguments":{"sessionId":"<sessionId>","expr":"1+2+3"}}'
```

```json
{"command":"eval","namespace":"debug","result":"6"}
```

**shutdown** — trigger graceful engine shutdown via SDL event:

```bash
curl -X POST http://localhost:9900/debug/command \
  -d '{"command":"shutdown","namespace":"debug","arguments":{"sessionId":"<sessionId>"}}'
```

### Error Response

```json
{"command":"eval","error":"session expired, please re-initialize"}
```

## Project Layout

```
noix-engine/
├── CMakeLists.txt
├── include/
│   ├── core/
│   │   ├── ArgsParser.h
│   │   ├── Logger.h
│   │   ├── NamespacedId.h
│   │   └── Sink.h
│   ├── debug/
│   │   ├── Command.h
│   │   ├── DebugServer.h
│   │   ├── EvalCommand.h
│   │   ├── HttpServer.h
│   │   └── ShutdownCommand.h
│   ├── resource/
│   │   └── ResourcePack.h
│   ├── runtime/
│   │   └── Application.h
│   └── script/
│       └── JSEngine.h
├── src/
│   ├── main.cpp
│   ├── core/
│   │   ├── ArgsParser.cpp
│   │   ├── Logger.cpp
│   │   ├── NamespacedId.cpp
│   │   └── Sink.cpp
│   ├── debug/
│   │   ├── DebugServer.cpp
│   │   ├── EvalCommand.cpp
│   │   ├── HttpServer.cpp
│   │   └── ShutdownCommand.cpp
│   ├── resource/
│   │   └── ResourcePack.cpp
│   ├── runtime/
│   │   └── Application.cpp
│   └── script/
│       └── JSEngine.cpp
└── third_party/
    ├── SDL/
    ├── SDL_net/
    ├── quickjs/
    └── cJSON/
```

## Conventions

- C++20 standard, Clang + Ninja toolchain
- Headers in `include/<module>/`, sources in `src/<module>/`
- Filenames match class names (e.g. `JSEngine.h`)
- camelCase for functions, `_prefix` for private members
- `noix::core`, `noix::debug`, `noix::resource`, `noix::runtime`, `noix::script` namespaces
- Identifiers use `namespace:name` format (e.g. `debug:eval`, `noix:textures/stone`)
