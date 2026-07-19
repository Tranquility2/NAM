cmake_minimum_required(VERSION 3.14)
project(NAM CXX)

# C++17 is the project-wide baseline.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# NAM targets opt into the warning policy explicitly, so FetchContent
# dependencies are never compiled with project warning flags.
include(nam_warnings)

# Optional sanitizer instrumentation driven by NAM_SANITIZE.
include(nam_sanitizers)

# Which components to build.
option(NAM_BUILD_CONSOLE  "Build the ASCII/console version of NAM"  ON)
option(NAM_BUILD_SDL      "Build the SDL graphical version of NAM"  OFF)
option(NAM_BUILD_SDL_TEST "Build the SDL sandbox (SDL_test)"        OFF)

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
