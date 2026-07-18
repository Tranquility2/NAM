# ASCII/console frontend for NAM.
#
# On Linux/macOS: uses termios + ANSI escape codes.
# On Windows:     future work (would use conio.h / windows.h).
add_executable(nam_console
    main.cpp
    console.cpp
    console.h
)

target_link_libraries(nam_console PRIVATE nam_core)

if(WIN32)
    message(WARNING
        "NAM_Console is currently ported to POSIX (termios). "
        "Windows support requires a windows.h/conio.h backend in console.cpp.")
endif()
