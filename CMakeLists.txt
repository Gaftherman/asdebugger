cmake_minimum_required(VERSION 3.21)
option(FORMAT_VALIDATION_ONLY "Configure formatting without native dependencies" OFF)
if(FORMAT_VALIDATION_ONLY)
    project(ASDebuggerFormat LANGUAGES NONE)
    include(cmake/FormatValidation.cmake)
    return()
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/metamod/cmake/Platform.cmake"
   OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/asext/cmake/SDK.cmake")
    message(FATAL_ERROR "Initialize dependencies with git submodule update --init --recursive.")
endif()
set(METAMOD_SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/metamod")
set(ASEXT_SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/asext")
include("${METAMOD_SOURCE_PATH}/cmake/Platform.cmake")
project(asdebugger VERSION 1.3.0 LANGUAGES C CXX)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()
include("${METAMOD_SOURCE_PATH}/cmake/Build.cmake")
mmfg_configure_install_layout()
add_subdirectory(metamod)
add_subdirectory(asext)

# Fetch cppdap (Debug Adapter Protocol implementation)
include(FetchContent)
FetchContent_Declare(
    cppdap
    GIT_REPOSITORY https://github.com/google/cppdap.git
    GIT_TAG main
    GIT_SUBMODULES "third_party/json"
    GIT_SHALLOW TRUE
)
set(CPPDAP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cppdap)

set(ASDEBUGGER_SOURCES
    src/asdebugger.cpp
    src/as_debugger.cpp
    src/as_debugger_dap.cpp
    src/as_helpers.cpp
    src/as_sc_debugger.cpp
    src/as_sc_workspace.cpp
    src/dllapi.cpp
    src/engine_api.cpp
    src/engine_hook.cpp
    src/h_export.cpp
    src/meta_api.cpp
    src/server_hook.cpp
)

if(WIN32)
    list(APPEND ASDEBUGGER_SOURCES src/dllmain.cpp)
endif()

add_library(asdebugger SHARED ${ASDEBUGGER_SOURCES})
mmfg_configure_target(asdebugger)
target_compile_features(asdebugger PRIVATE cxx_std_20)
target_link_libraries(asdebugger PRIVATE ASExt::SDK cppdap)
target_compile_definitions(asdebugger PRIVATE ASDEBUGGER_VERSION="${PROJECT_VERSION}")

if(WIN32)
    target_link_libraries(asdebugger PRIVATE ws2_32)
    target_link_options(asdebugger PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/asdebugger.def")
else()
    target_compile_options(asdebugger PRIVATE -fexceptions)
    target_link_libraries(asdebugger PRIVATE pthread dl)
endif()

mmfg_assign_source_groups(asdebugger)
install(FILES assets/plugins.ini.example DESTINATION addons/metamod)
install(FILES examples/asdebugger_example.as DESTINATION scripts/plugins)
install(FILES README.md LICENSE THIRD_PARTY.md DESTINATION .)
install(FILES metamod/LICENSE DESTINATION licenses RENAME metamod-LICENSE)
install(FILES asext/LICENSE DESTINATION licenses RENAME asext-LICENSE)

option(ASDEBUGGER_BUILD_TESTS "Build the plugin ABI smoke test" ON)
if(ASDEBUGGER_BUILD_TESTS)
    enable_testing()
    add_executable(plugin-smoke tests/plugin_smoke.cpp)
    target_compile_features(plugin-smoke PRIVATE cxx_std_17)
    target_link_libraries(plugin-smoke PRIVATE ASExt::SDK ${CMAKE_DL_LIBS})
    if(NOT WIN32)
        target_compile_definitions(plugin-smoke PRIVATE PLATFORM_POSIX LINUX _LINUX)
    endif()
    add_test(NAME plugin-smoke COMMAND plugin-smoke "$<TARGET_FILE:asdebugger>")
endif()
include(cmake/FormatValidation.cmake)
