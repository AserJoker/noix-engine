# noix-engine

A lightweight game engine built with C++20, featuring SDL3-based windowing, QuickJS scripting, and DAP-based remote debugging.

## Architecture

```
noix-engine
├── core/          Fundamental utilities
│   ├── ArgsParser     Command-line argument parser (--key=value, -key value, positional)
│   ├── Logger         Logging with std::format, SDL_Log redirect, and pluggable sinks
│   ├── NamespacedId   Value type for namespace:name identifiers
│   ├── Value          JSON value wrapper (std::variant-based, cJSON for serialization)
│   └── Sink           Output targets — ConsoleSink (colored) and FileSink (plain)
├── debug/         Remote debug service
│   ├── DebugServer    REST API command dispatch (HTTP-based)
│   ├── DapBridge      DAP debug bridge for QuickJS (TCP protocol)
│   └── DapSocket      Thread-safe TCP socket wrapper
├── runtime/       Application lifecycle & managers
│   ├── Application    SDL3 window, main event loop, graceful shutdown
│   ├── AssetManager   Multi-pack asset resolver with priority overlay
│   ├── ConfigManager  JSON config storage with disk persistence
│   ├── LocaleManager  i18n support (namespace-scoped .lang files)
│   └── SaveManager    Persistent save-data storage (saves/<slot>/<ns>/...)
└── script/        Scripting runtime
    └── ScriptEngine   QuickJS-ng wrapper (JSRuntime + JSContext)
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
git clone --recurse-submodules https://github.com/AserJoker/noix-engine.git
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

## Script Modules

The engine exposes native C modules to QuickJS scripts:

| Module | Exports | Description |
|--------|---------|-------------|
| `noix:logger` | `info`, `warn`, `error`, `debug`, `trace` | Logging |
| `noix:debug` | `registerCommand(name, version, handler)` | REST API command registration |
| `noix:locale` | `i18n`, `setLang`, `getLang`, `addNamespace`, `removeNamespace`, `reset` | Internationalization |
| `noix:config` | `get`, `set`, `has`, `remove`, `list` | Config read/write |
| `noix:save` | `save`, `load`, `exists`, `remove`, `deleteSlot`, `listSlots` | Save data persistence |

### Usage Examples

```js
import { info } from "noix:logger";
import { registerCommand } from "noix:debug";
import { i18n } from "noix:locale";
import * as config from "noix:config";
import * as save from "noix:save";

// i18n
info(i18n("noix:system.window.title", "noix-engine"));

// Config
const appCfg = config.get("noix:application", { window: { mode: "windowed" } });
config.set("my:settings", { enabled: true, count: 0 });

// Save data
save.save("world1", "noix:player/data.dat", JSON.stringify({hp:100}));
const data = save.load("world1", "noix:player/data.dat");

// Register REST API command
registerCommand("script/echo", "v1", (req) => ({ echo: req }));
```

## DAP Debug Bridge

The engine includes a DAP debug server for IDE integration, communicating over TCP.

### Quick Start

```bash
cmake --build build
./dist/noix-engine --dap-port 9876 --headless
```

Connect from VS Code or any DAP client to `localhost:9876`.

### DAP Protocol Support

| DAP Request | Behavior |
|---|---|
| `initialize` | Capability exchange |
| `launch` | Attach to running script (no re-execution) |
| `attach` | Attach with default entry.js |
| `disconnect` | Clean up, keep session alive |
| `setBreakpoints` | Set/clear breakpoints |
| `setExceptionBreakpoints` | Pause on exceptions |
| `continue` / `next` / `stepIn` / `stepOut` | Execution control |
| `pause` | Pause on next statement |
| `stackTrace` / `scopes` / `variables` | Stack inspection |
| `evaluate` | Evaluate expression in frame scope |
| `threads` / `loadedSources` | Thread and source listing |

### DAP Events

| Event | Trigger |
|---|---|
| `stopped` | Breakpoint hit, step complete, debugger statement, exception |
| `continued` | Execution resumed |
| `terminated` | Session disconnected |
| `output` | Console output |

## AssetManager

Assets are organized in Minecraft-style directory layout with multi-pack priority overlay.

### Directory Layout

```
<pack-root>/assets/<namespace>/<path>
```

For example:

```
basePath/assets/noix/textures/stone.png      # engine default
modpack/assets/noix/textures/stone.png       # override
modpack/assets/mymod/sprites/player.png      # new resource
```

### Access

```cpp
using namespace noix::core;
using namespace noix::runtime;

AssetManager assets(basePath);
assets.addPack("modpack");  // higher priority than default

auto path = assets.resolve(NamespacedId("noix", "textures/stone.png"));
bool has = assets.exists(NamespacedId("mymod", "sprites/player.png"));
```

### Priority Rules

- The engine's default resource directory (`basePath`) has the **lowest** priority.
- Packs added via `addPack()` have higher priority, in **addition order** (later = higher).
- `resolve()` searches from highest to lowest priority, returning the first match.

## ConfigManager

JSON-based configuration with disk persistence, using `core::Value` as the data model.

```cpp
using namespace noix::core;
using namespace noix::runtime;

ConfigManager mgr(configDir);
mgr.loadAll();

Value cfg = mgr.get(NamespacedId("noix", "application"));
Value defaults = Value::object();
defaults.asObject()["window"] = Value::object();
mgr.set(NamespacedId("noix", "application"), defaults);

mgr.saveAll();
```

## LocaleManager

Internationalization support via namespace-scoped `.lang` files.

```cpp
using namespace noix::runtime;

LocaleManager locale(&assets);
locale.addNamespace("noix");
locale.setLang("en_US");

std::string title = locale.i18n("noix:system.window.title", "fallback");
```

### .lang File Format

```
# Comment
system.window.title = "noix-window"
item.sword.name = "Iron Sword"
```

Keys in `noix:i18n/en_US.lang` become `noix:system.window.title`, `noix:item.sword.name`.

## SaveManager

Persistent save-data storage with namespace-scoped file paths.

```cpp
using namespace noix::runtime;

SaveManager saver(basePath);
saver.save("world1", NamespacedId("noix", "player/inventory.dat"), data);
std::string data = saver.load("world1", NamespacedId("noix", "player/inventory.dat"));
saver.deleteSlot("world1");
```

File path: `saves/world1/noix/player/inventory.dat`

## Logging

The engine uses `core::Logger` (singleton) with `std::format_string` for compile-time format checking. Output is dispatched to pluggable sinks:

- **ConsoleSink** — colored level tags on stderr
- **FileSink** — plain text to file

SDL internal logs are redirected into the Logger via `installSdlRedirect()`.

```cpp
auto& log = core::Logger::instance();
log.addSink(std::make_shared<core::ConsoleSink>());
log.addSink(std::make_shared<core::FileSink>("engine.log"));
log.setLevel(core::LogLevel::Trace);
log.installSdlRedirect();

log.info("server started on port {}", port);
```

## NamespacedId

All identifiers in the engine use the `namespace:name` format, inspired by Minecraft's resource location system. The system namespace is `noix`.

```cpp
NamespacedId id1("noix", "textures/stone");  // → "noix:textures/stone"
NamespacedId id2("textures/stone");            // → ns="noix"
auto id3 = NamespacedId::parse("mymod:sprites/player");
```

## Testing

### Unit Tests

```bash
cmake --build build --target test_unit
./dist/test_unit
```

### Integration Tests

```bash
cmake --build build --target test_integration
./dist/test_integration
```

### DAP Protocol Tests (Node.js)

```bash
node tests/dap/dap_client_test.js
node tests/dap/dap_multifile_test.js
```

## Project Layout

```
noix-engine/
├── CMakeLists.txt
├── include/
│   ├── core/                Fundamental utilities
│   ├── debug/               Debug server + DAP bridge
│   ├── runtime/             Application & managers
│   └── script/              Script engine
├── src/
│   ├── main.cpp
│   ├── core/
│   ├── debug/
│   ├── runtime/
│   └── script/
├── assets/
│   └── noix/i18n/en_US.lang
├── scripts/
│   ├── entry.ts
│   └── noix-modules/        TypeScript declarations
├── tests/
│   ├── dap/                 DAP protocol tests (Node.js)
│   ├── integration/         C++ integration tests
│   └── unit/                C++ unit tests
└── third_party/
    ├── quickjs/             QuickJS-ng
    ├── cJSON/               JSON parser
    ├── SDL/                 SDL3
    └── SDL_net/             SDL_net
```

## Conventions

- C++20 standard, MSVC (Windows) / Clang + Ninja (Linux)
- Headers in `include/<module>/`, sources in `src/<module>/`
- Filenames match class names (e.g. `AssetManager.h`)
- camelCase for functions, `_prefix` for private members
- `noix::core`, `noix::debug`, `noix::runtime`, `noix::script` namespaces
- Identifiers use `namespace:name` format (e.g. `noix:textures/stone`)
