cmake_minimum_required(VERSION 3.14)
project(NAM CXX)

# C++17 is the project-wide baseline. Individual targets may still request the
# feature explicitly (nam_core does), but this makes 17 the floor everywhere.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Warning policy lives in a private INTERFACE target (cmake/nam_warnings.cmake).
# NAM targets link it to opt into warnings-as-errors; FetchContent dependencies
# never do, so their code is not held to our flags.
include(nam_warnings)

# Optional sanitizer instrumentation, driven by the NAM_SANITIZE cache variable
# (set by the asan-ubsan / msvc-asan presets). A no-op for normal builds. It is
# included before any target is defined so all NAM code is instrumented.
include(nam_sanitizers)

# Which components to build.
option(NAM_BUILD_CONSOLE  "Build the ASCII/console version of NAM"  ON)
option(NAM_BUILD_SDL      "Build the SDL graphical version of NAM"  OFF)
option(NAM_BUILD_SDL_TEST "Build the SDL sandbox (SDL_test)"        OFF)
option(NAM_BUILD_TESTS    "Build the unit and integration tests"    OFF)

add_subdirectory(core)

if(NAM_BUILD_CONSOLE)
    add_subdirectory(NAM_Console)
endif()

if(NAM_BUILD_SDL)
    add_subdirectory(NAM_SDL)
endif()

if(NAM_BUILD_SDL_TEST)
    add_subdirectory(SDL_test)
endif()

# Tests are gated behind NAM_BUILD_TESTS so a plain `cmake --build` of the game
# never pulls the test framework. enable_testing() must be called at the top
# level for `ctest` in the build root to discover tests defined in tests/.
if(NAM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
