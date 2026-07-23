cmake_minimum_required(VERSION 3.20)

add_executable(test_debug
    test_debug.c
)

target_include_directories(test_debug PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/quickjs
)

target_link_libraries(test_debug PRIVATE
    qjs
)

set_target_properties(test_debug PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist
)
