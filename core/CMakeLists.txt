# Shared, frontend-agnostic game logic.
#
# nam_core contains no terminal, ANSI, or presentation code; it exposes typed
# map, terrain, movement, and parsing APIs consumable by any frontend.
add_library(nam_core STATIC
    coordinates.h
    terrain.h
    direction.h
    move_outcome.h
    map.h
    map.cpp
    map_parser.h
    map_parser.cpp
    game_state.h
    game_state.cpp
)

# The core uses C++17 (std::variant, std::string_view, std::optional). Requesting
# the feature here raises the standard for nam_core and everything that links it,
# independent of the project-wide default.
target_compile_features(nam_core PUBLIC cxx_std_17)

target_include_directories(nam_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Warnings-as-errors for NAM code, via the shared private interface target.
target_link_libraries(nam_core PRIVATE nam_warnings)
