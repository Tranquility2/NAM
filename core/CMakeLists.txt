# Shared, frontend-agnostic game logic.
add_library(nam_core STATIC
    map.cpp
    map.h
    resources.h
)

target_include_directories(nam_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
