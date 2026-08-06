# Core rules, held to the frontend-neutrality contract by the link line: these
# tests see nam_core and nothing else, so a rule that quietly grew a dependency
# on console code cannot compile here.
nam_add_test_executable(nam_core_tests
    LINK nam_core
    LABELS "core;unit"
    SOURCES
        map_parser.cpp
        terrain_coords.cpp
        map_rendering.cpp
        movement.cpp
        game_events.cpp
        visibility.cpp
        objective.cpp
        level_tier.cpp
        level_feature.cpp
        set_piece.cpp
        world_generation.cpp
        expedition.cpp
        expedition_score.cpp
        pcg32.cpp)
