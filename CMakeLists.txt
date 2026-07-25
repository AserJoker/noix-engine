cmake_minimum_required(VERSION 3.20)
project(noix-engine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- SDL3 ----
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/SDL)

# ---- quickjs-ng ----
set(ASMJS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/quickjs)

# ---- cJSON ----
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CUSTOM_COMPILER_FLAGS OFF CACHE BOOL "" FORCE)
set(CJSON_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/cJSON)

# ---- SDL3_net ----
set(SDLNET_SAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/SDL_net)

# ---- noix-engine ----
add_executable(noix-engine
    src/main.cpp
    src/core/ArgsParser.cpp
    src/core/Config.cpp
    src/core/ConfigManager.cpp
    src/core/Logger.cpp
    src/core/NamespacedId.cpp
    src/core/Sink.cpp
    src/debug/BreakpointCommand.cpp
    src/debug/ConfigGetCommand.cpp
    src/debug/DebugControlCommand.cpp
    src/debug/DebugEvalCommand.cpp
    src/debug/DebugEvalFrameCommand.cpp
    src/debug/DebugServer.cpp
    src/debug/DebugStatusCommand.cpp
    src/debug/EvalCommand.cpp
    src/debug/HttpServer.cpp
    src/debug/ShutdownCommand.cpp
    src/debug/StackTraceCommand.cpp
    src/debug/DebugVariablesCommand.cpp
    src/debug/SourceMapCommand.cpp
    src/debug/DebugRunCommand.cpp
    src/resource/ResourcePack.cpp
    src/runtime/Application.cpp
    src/script/DebugAgent.cpp
    src/script/NativeModules.cpp
    src/script/ScriptEngine.cpp
)

target_include_directories(noix-engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cJSON
)

target_link_libraries(noix-engine PRIVATE
    SDL3::SDL3
    SDL3_net::SDL3_net
    qjs
    cjson
)

set_target_properties(noix-engine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist
)

# ---- CDP Debug Bridge ----
add_executable(cdp-debug-bridge
    src/debug/CdpMain.cpp
    src/debug/Base64.cpp
    src/debug/Sha1.cpp
    src/debug/WebSocketServer.cpp
    src/debug/JsDebugBridge.cpp
    src/debug/CdpDispatcher.cpp
    src/debug/CdpSession.cpp
    # Also needs core sources for Logger
    src/core/Logger.cpp
    src/core/Sink.cpp
)

target_include_directories(cdp-debug-bridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cJSON
)

target_link_libraries(cdp-debug-bridge PRIVATE
    SDL3::SDL3
    SDL3_net::SDL3_net
    qjs
    cjson
)

set_target_properties(cdp-debug-bridge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist
)

# ---- DAP Test Bridge ----
add_executable(dap-test-bridge
    src/debug/DapTestBridge.cpp
)

target_include_directories(dap-test-bridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cJSON
)

target_link_libraries(dap-test-bridge PRIVATE
    qjs
    cjson
)

set_target_properties(dap-test-bridge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist
)

# ---- Debug Test ----
add_subdirectory(tests/debug_test)

# ---- Tests ----
option(NOIX_BUILD_TESTS "Build tests" ON)
if(NOIX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
