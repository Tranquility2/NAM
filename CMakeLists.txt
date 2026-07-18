cmake_minimum_required(VERSION 3.10)
project(nam CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

add_executable(nam
    NAM_draft/main.cpp
    NAM_draft/map.cpp
    NAM_draft/console.cpp
)

target_include_directories(nam PRIVATE NAM_draft)
