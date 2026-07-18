cmake_minimum_required(VERSION 3.14)
project(NAM CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

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
