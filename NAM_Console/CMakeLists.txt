# ASCII/console frontend for NAM.
#
# Rules and state live in nam_core; this target is a thin, portable frontend:
# a semantic input decoder, an adaptive renderer, typed settings, and a RAII
# terminal session whose platform backend is selected here (never with
# per-call platform branches inside the application code).
#
# The frontend is split into a static library (nam_console_lib) plus a tiny
# main.cpp. The library holds every unit of behaviour — CLI parsing, the input
# decoder, the renderer, the HUD, message mapping, and the plain/interactive
# app loop — so the test suite can link and exercise it directly without a real
# terminal. The executable is just the entry point.

set(nam_console_lib_sources
    console_app.cpp
    console_app.h
    app_state.cpp
    app_state.h
    input.h
    input_decoder.cpp
    input_decoder.h
    journal.cpp
    journal.h
    messages.cpp
    messages.h
    renderer.cpp
    renderer.h
    settings.cpp
    settings.h
    frame.h
    terminal.h
    terminal.cpp
)

# Exactly one terminal backend is compiled per platform.
if(WIN32)
    list(APPEND nam_console_lib_sources terminal_windows.cpp)
else()
    list(APPEND nam_console_lib_sources terminal_posix.cpp)
endif()

add_library(nam_console_lib STATIC ${nam_console_lib_sources})

target_link_libraries(nam_console_lib PUBLIC nam_core)
target_link_libraries(nam_console_lib PRIVATE nam_warnings)

# The frontend uses C++17 (std::optional, std::string_view, std::variant).
# nam_core already requires it transitively, but state it directly so the target
# is correct on its own.
target_compile_features(nam_console_lib PUBLIC cxx_std_17)

target_include_directories(nam_console_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(nam_console main.cpp)
target_link_libraries(nam_console PRIVATE nam_console_lib nam_warnings)
