# SDL sandbox. Uses CMake FetchContent to download and build a pinned
# SDL2 + SDL2_image + SDL2_ttf so every OS gets the exact same version.
#
# Bump the GIT_TAG values below to upgrade.
include(FetchContent)

set(SDL2_DISABLE_INSTALL       ON CACHE BOOL "" FORCE)
set(SDL2IMAGE_INSTALL          OFF CACHE BOOL "" FORCE)
set(SDL2TTF_INSTALL            OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_VENDORED         ON  CACHE BOOL "" FORCE)
set(SDL2TTF_VENDORED           ON  CACHE BOOL "" FORCE)
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

add_executable(sdl_test main.cpp)

target_link_libraries(sdl_test PRIVATE
    SDL2::SDL2
    SDL2::SDL2main
    SDL2_image::SDL2_image
    SDL2_ttf::SDL2_ttf
)

# Copy runtime assets (font + logo) next to the built binary so the
# hard-coded relative paths in main.cpp resolve.
add_custom_command(TARGET sdl_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/Sans.ttf
        $<TARGET_FILE_DIR:sdl_test>/Sans.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/Sdl-logo.png
        $<TARGET_FILE_DIR:sdl_test>/Sdl-logo.png
)
