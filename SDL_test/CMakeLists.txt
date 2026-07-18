# SDL sandbox — multi-OS sub-project.
#
# The build works on Linux, macOS, and Windows because:
#   * SDL2, SDL2_image, SDL2_ttf are downloaded and built from source
#     via CMake FetchContent (identical pinned version on every OS).
#   * The source uses only cross-platform SDL2 APIs (no windows.h etc.).
#   * SDL2main is linked so main() works as-is on all platforms
#     (SDL wraps it into WinMain on Windows).
#   * Assets are located via SDL_GetBasePath() so they load no matter
#     what the current working directory is.
#
# Bump the GIT_TAG values below to upgrade SDL2.
include(FetchContent)

set(SDL2_DISABLE_INSTALL       ON  CACHE BOOL "" FORCE)
set(SDL2IMAGE_INSTALL          OFF CACHE BOOL "" FORCE)
set(SDL2TTF_INSTALL            OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_VENDORED         ON  CACHE BOOL "" FORCE)
set(SDL2TTF_VENDORED           ON  CACHE BOOL "" FORCE)

# Build SDL2 as a static library so the resulting binary is self-contained
# (no need to ship SDL2.dll / libSDL2.so alongside sdl_test).
set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)

FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.11
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(
    SDL2_image
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
    GIT_TAG        release-2.8.2
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(
    SDL2_ttf
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
    GIT_TAG        release-2.22.0
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(SDL2 SDL2_image SDL2_ttf)

set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED})

# On Windows, WIN32 produces a GUI subsystem exe (no console window).
# We keep it as a plain console app on all platforms so std::cout logging
# is visible during development; switch to `add_executable(sdl_test WIN32 ...)`
# later if you want to hide the console on Windows.
add_executable(sdl_test main.cpp)

target_link_libraries(sdl_test PRIVATE
    SDL2::SDL2main   # cross-platform entry-point shim (WinMain on Windows)
    SDL2::SDL2-static
    SDL2_image::SDL2_image-static
    SDL2_ttf::SDL2_ttf-static
)

# Copy runtime assets (font + logo) next to the built binary so the
# SDL_GetBasePath() lookup in main.cpp finds them on every OS.
add_custom_command(TARGET sdl_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/Sans.ttf
        $<TARGET_FILE_DIR:sdl_test>/Sans.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/Sdl-logo.png
        $<TARGET_FILE_DIR:sdl_test>/Sdl-logo.png
)

