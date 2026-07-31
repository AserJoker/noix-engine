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
    src/core/Logger.cpp
    src/core/NamespacedId.cpp
    src/core/Sink.cpp
    src/core/SemVer.cpp
    src/core/Value.cpp
    src/debug/DapBridgeCore.cpp
    src/debug/DapBridgeHandlers.cpp
    src/debug/DapBridgeExecute.cpp
    src/debug/DapObjectRefStore.cpp
    src/debug/DapSocket.cpp
    src/debug/DapBridgeUtils.cpp
    src/debug/SourceMap.cpp
    src/debug/DapServer.cpp
    src/debug/DebugServer.cpp
    src/debug/ScriptCallbackCommand.cpp
    src/runtime/Application.cpp
    src/runtime/EventBus.cpp
    src/runtime/ModManager.cpp
    src/runtime/AssetManager.cpp
    src/runtime/ConfigManager.cpp
    src/runtime/LocaleManager.cpp
    src/runtime/SaveManager.cpp
    src/script/NativeModules.cpp
    src/script/modules/LoggerModule.cpp
    src/script/modules/DebugModule.cpp
    src/script/modules/LocaleModule.cpp
    src/script/modules/ConfigModule.cpp
    src/script/modules/SaveModule.cpp
    src/script/modules/EventBusModule.cpp
    src/script/modules/ModManagerModule.cpp
    src/script/ScriptEngine.cpp
    src/video/Renderer.cpp
)

target_include_directories(noix-engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cJSON
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/quickjs
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

# ---- Shader compilation ----
# Uses glslangValidator from system PATH or VULKAN_SDK to compile GLSL to SPIR-V.
# .spv files are deployed to dist/assets/ for runtime loading via AssetManager.
find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS "$ENV{VULKAN_SDK}/bin"
    DOC "Path to glslangValidator executable"
)

if(NOT GLSLANG_VALIDATOR)
    message(WARNING "glslangValidator not found — shader compilation disabled. "
                    "Install Vulkan SDK (https://vulkan.lunarg.com) and ensure "
                    "glslangValidator is in PATH or set VULKAN_SDK env var.")
else()
    message(STATUS "Found glslangValidator: ${GLSLANG_VALIDATOR}")

    set(SHADER_SRC_DIR ${CMAKE_SOURCE_DIR}/assets/noix/shaders)
    set(SHADER_BUILD_DIR ${CMAKE_BINARY_DIR}/shaders)
    set(SHADER_DIST_DIR ${CMAKE_SOURCE_DIR}/dist/assets/noix/shaders)

    if(EXISTS "${SHADER_SRC_DIR}")
        file(GLOB_RECURSE SHADER_SOURCES LIST_DIRECTORIES false
             "${SHADER_SRC_DIR}/*.vert"
             "${SHADER_SRC_DIR}/*.frag"
             "${SHADER_SRC_DIR}/*.comp")

        set(SHADER_OUTPUTS "")
        foreach(SRC ${SHADER_SOURCES})
            file(RELATIVE_PATH REL_PATH "${SHADER_SRC_DIR}" ${SRC})
            get_filename_component(REL_DIR ${REL_PATH} DIRECTORY)
            get_filename_component(BASENAME ${SRC} NAME_WE)

            set(SPV_FILE "${SHADER_BUILD_DIR}/${REL_DIR}/${BASENAME}.spv")

            add_custom_command(
                OUTPUT ${SPV_FILE}
                COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BUILD_DIR}/${REL_DIR}"
                COMMAND ${GLSLANG_VALIDATOR} -V "${SRC}" -o "${SPV_FILE}"
                DEPENDS ${SRC}
                COMMENT "Compiling shader: ${REL_PATH}"
            )
            list(APPEND SHADER_OUTPUTS ${SPV_FILE})
        endforeach()

        add_custom_target(compile_shaders ALL DEPENDS ${SHADER_OUTPUTS})

        foreach(SPV ${SHADER_OUTPUTS})
            file(RELATIVE_PATH REL_PATH "${SHADER_BUILD_DIR}" ${SPV})
            set(DST "${SHADER_DIST_DIR}/${REL_PATH}")
            add_custom_command(
                TARGET noix-engine POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SPV}" "${DST}"
                COMMENT "Deploying shader: ${REL_PATH}"
            )
        endforeach()
    endif()
endif()

# ---- Post-build: copy assets to dist (incremental) ----
configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/copy_assets.cmake.in
    ${CMAKE_BINARY_DIR}/copy_assets.cmake
    @ONLY
)

add_custom_command(TARGET noix-engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/copy_assets.cmake
    COMMENT "Copying assets to dist (incremental)"
)

# ---- Debug Test ----
add_subdirectory(tests/debug_test)

# ---- Tests ----
option(NOIX_BUILD_TESTS "Build tests" ON)
if(NOIX_BUILD_TESTS)
    add_subdirectory(tests)
endif()