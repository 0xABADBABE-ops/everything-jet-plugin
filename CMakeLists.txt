cmake_minimum_required(VERSION 3.20)

# ---- Source paths ----
# The Everything3 SDK v3 (Everything3_x64.dll, named-pipe IPC3) is purpose-built
# for Everything 1.5. It replaces the legacy v2 SDK (Everything64.dll, WM_COPYDATA).
set(EVERYTHING_SDK_DIR "" CACHE PATH
    "Path to the Everything3 SDK root (must contain include/, lib/, and dll/)"
)

if(NOT EVERYTHING_SDK_DIR)
    if(DEFINED ENV{EVERYTHING_SDK_DIR} AND EXISTS "$ENV{EVERYTHING_SDK_DIR}")
        set(EVERYTHING_SDK_DIR "$ENV{EVERYTHING_SDK_DIR}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../SDK-3.0.0.9")
        set(EVERYTHING_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../SDK-3.0.0.9")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../SDK")
        set(EVERYTHING_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../SDK")
    endif()
endif()

set(SDK_DIR "${EVERYTHING_SDK_DIR}")

# Detect v3 vs v2 SDK by checking for the v3 header/lib
set(HAS_V3_SDK FALSE)
if(EXISTS "${SDK_DIR}/include/Everything3.h"
   AND EXISTS "${SDK_DIR}/lib/Everything3_x64.lib"
   AND EXISTS "${SDK_DIR}/dll/Everything3_x64.dll")
    set(HAS_V3_SDK TRUE)
endif()

if(NOT HAS_V3_SDK)
    message(FATAL_ERROR
        "Everything3 SDK v3 not found.\n"
        "Set EVERYTHING_SDK_DIR to the SDK root containing include/, lib/, dll/.\n"
        "Expected files:\n"
        "  include/Everything3.h\n"
        "  lib/Everything3_x64.lib\n"
        "  dll/Everything3_x64.dll\n"
        "Current value: '${SDK_DIR}'"
    )
endif()

project(everything-mcp
    VERSION 2.0.0
    DESCRIPTION "MCP server exposing Everything file search to AI agents"
    LANGUAGES CXX
)

# ---- C++23 (or latest available) ----
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use latest available standard flag (C++23 on MSVC 14.50)
if(MSVC)
    add_compile_options(/std:c++latest /W4 /permissive- /utf-8 /EHsc)
    # Disable specific noisy warnings
    add_compile_options(/wd4459 /wd4244 /wd4267 /wd4189)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---- MCP Server executable ----
add_executable(everything-mcp
    src/main.cpp
    src/mcp_server.cpp
    src/everything_wrapper.cpp
    src/content_search.cpp
    src/smart_query.cpp
    src/actions.cpp
    src/file_hashing.cpp
    src/file_preview.cpp
)

target_include_directories(everything-mcp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${SDK_DIR}/include
)

target_link_directories(everything-mcp PRIVATE
    ${SDK_DIR}/lib
)

target_link_libraries(everything-mcp PRIVATE
    Everything3_x64
    ole32
    oleaut32
    Bcrypt
    Version
)

# Copy the DLL next to the executable so it can be found at runtime
add_custom_command(TARGET everything-mcp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SDK_DIR}/dll/Everything3_x64.dll
        $<TARGET_FILE_DIR:everything-mcp>
    COMMENT "Copying Everything3_x64.dll to output directory"
)

# ---- Test executable ----
enable_testing()

add_executable(everything-mcp-tests
    tests/test_main.cpp
    src/mcp_server.cpp
    src/everything_wrapper.cpp
    src/content_search.cpp
    src/smart_query.cpp
    src/actions.cpp
    src/file_hashing.cpp
    src/file_preview.cpp
)

target_include_directories(everything-mcp-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${SDK_DIR}/include
)

target_link_directories(everything-mcp-tests PRIVATE
    ${SDK_DIR}/lib
)

target_link_libraries(everything-mcp-tests PRIVATE
    Everything3_x64
    ole32
    oleaut32
    Bcrypt
    Version
)

add_custom_command(TARGET everything-mcp-tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SDK_DIR}/dll/Everything3_x64.dll
        $<TARGET_FILE_DIR:everything-mcp-tests>
    COMMENT "Copying Everything3_x64.dll to test output directory"
)

add_test(NAME all_tests COMMAND everything-mcp-tests)
