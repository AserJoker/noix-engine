cmake_minimum_required(VERSION 3.20)

# CMake 4.x on Windows with Clang requires RC compiler (freetype's ftver.rc)
if(CMAKE_C_COMPILER MATCHES "clang" AND NOT DEFINED CMAKE_RC_COMPILER)
    get_filename_component(_clang_bin "${CMAKE_C_COMPILER}" DIRECTORY)
    find_program(LLVM_RC llvm-rc HINTS "${_clang_bin}" NO_DEFAULT_PATH)
    if(LLVM_RC)
        set(CMAKE_RC_COMPILER "${LLVM_RC}" CACHE FILEPATH "RC compiler" FORCE)
    endif()
    unset(_clang_bin)
endif()

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

# ---- SDL3_image ----
set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/SDL_image)

# ---- SDL3_ttf ----
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ ON CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/SDL_ttf)

# ---- GLM ----
add_subdirectory(third_party/glm)

# ---- noix-engine ----
add_executable(noix-engine
    src/main.cpp
    src/core/ArgsParser.cpp
    src/core/Logger.cpp
    src/core/NamespacedId.cpp
    src/core/Sink.cpp
    src/core/SemVer.cpp
    src/core/Resource.cpp
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
    src/video/Texture.cpp
    src/video/Image.cpp
    src/video/Shader.cpp
    src/video/Pipeline.cpp
    src/video/Nxmd.cpp
    src/video/Mesh.cpp
    src/video/Material.cpp
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
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    glm::glm
    qjs
    cjson
)

set_target_properties(noix-engine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist
)

# ---- Shader compilation ----
# Uses glslangValidator from system PATH or VULKAN_SDK to compile GLSL to SPIR-V.
# Compiled SPIR-V is embedded into the binary via BuiltinShaders.h (no runtime disk access).
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

    set(SHADER_SRC_DIR ${CMAKE_SOURCE_DIR}/shaders)
    set(SHADER_BUILD_DIR ${CMAKE_BINARY_DIR}/shaders)

    if(EXISTS "${SHADER_SRC_DIR}")
        file(GLOB_RECURSE SHADER_SOURCES LIST_DIRECTORIES false
             "${SHADER_SRC_DIR}/*.vert"
             "${SHADER_SRC_DIR}/*.frag"
             "${SHADER_SRC_DIR}/*.comp")

        set(SHADER_OUTPUTS "")
        foreach(SRC ${SHADER_SOURCES})
            file(RELATIVE_PATH REL_PATH "${SHADER_SRC_DIR}" ${SRC})

            set(SPV_FILE "${SHADER_BUILD_DIR}/${REL_PATH}.spv")

            add_custom_command(
                OUTPUT ${SPV_FILE}
                COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BUILD_DIR}/${REL_PATH}"
                COMMAND ${GLSLANG_VALIDATOR} -V "${SRC}" -o "${SPV_FILE}"
                DEPENDS ${SRC}
                COMMENT "Compiling shader: ${REL_PATH}"
            )
            list(APPEND SHADER_OUTPUTS ${SPV_FILE})
        endforeach()

        add_custom_target(compile_shaders ALL DEPENDS ${SHADER_OUTPUTS})

        # Generate embedded SPIR-V header for builtin shaders
        set(EMBED_HEADER ${CMAKE_SOURCE_DIR}/src/video/BuiltinShaders.h)
        add_custom_command(
            OUTPUT ${EMBED_HEADER}
            COMMAND ${CMAKE_COMMAND}
                -DOUTPUT=${EMBED_HEADER}
                -DSPIRV_DIR=${SHADER_BUILD_DIR}
                -P ${CMAKE_SOURCE_DIR}/cmake/embed_spirv.cmake
            DEPENDS ${SHADER_OUTPUTS}
            COMMENT "Generating embedded SPIR-V header"
        )
        add_custom_target(embed_shaders DEPENDS ${EMBED_HEADER})
        add_dependencies(noix-engine embed_shaders)
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