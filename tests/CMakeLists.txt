set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/googletest ${CMAKE_BINARY_DIR}/googletest)

enable_testing()

# ---- Unit Tests ----
add_executable(test_unit
    unit/test_args_parser.cpp
    unit/test_config.cpp
    unit/test_config_manager.cpp
    unit/test_namespaced_id.cpp
    unit/test_resource_pack.cpp
    unit/test_sink.cpp
    unit/test_sourcemap.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ArgsParser.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Config.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ConfigManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/core/NamespacedId.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Sink.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Value.cpp
    ${CMAKE_SOURCE_DIR}/src/resource/ResourcePack.cpp
    ${CMAKE_SOURCE_DIR}/src/debug/SourceMap.cpp
)
target_link_libraries(test_unit PRIVATE GTest::gtest_main SDL3::SDL3 cjson)
target_include_directories(test_unit PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/cJSON
)

# ---- Integration Tests (DebugServer shell) ----
add_executable(test_integration
    integration/helpers/HttpClient.cpp
    integration/test_debug_server.cpp
)
target_link_libraries(test_integration PRIVATE GTest::gtest_main SDL3::SDL3 SDL3_net::SDL3_net)
target_include_directories(test_integration PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/integration/helpers)

# ---- Output to dist/ ----
set_target_properties(test_unit test_integration PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist
)

include(GoogleTest)
gtest_discover_tests(test_unit)
gtest_discover_tests(test_integration)
