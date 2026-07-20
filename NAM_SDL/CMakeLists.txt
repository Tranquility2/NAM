# Placeholder: NAM_SDL is the future graphical (SDL) version of NAM.
# It is intentionally not included by the top-level build until it has sources
# and creates a real `nam_sdl` target.
#
# When you're ready to start, add sources here and link against nam_core:
#
#     add_executable(nam_sdl main.cpp)
#     target_link_libraries(nam_sdl PRIVATE nam_core SDL3::SDL3 SDL3_image::SDL3_image SDL3_ttf::SDL3_ttf)
#
# For a working SDL bootstrap/reference project (SDL provided via CMake
# FetchContent, cross-platform CI, and a smoke test), see:
#
#     https://github.com/Tranquility2/sdl-intro

message(STATUS "NAM_SDL is a scaffold; no sources yet.")
