# nam_warnings: a private INTERFACE target carrying the project's warning policy.
#
# Only NAM-owned targets (nam_core, nam_console_lib, nam_console, the test
# executables) link this target, so warnings-as-errors never apply to
# FetchContent dependencies such as doctest or SDL2 — their headers and sources
# are compiled without our strict flags. This is the mechanism the quality plan
# calls for: "a private nam_warnings interface target ... never FetchContent
# dependencies."
#
# Usage:
#     target_link_libraries(my_nam_target PRIVATE nam_warnings)
#
# Because the link is PRIVATE and the options live on an INTERFACE target, the
# flags apply to that target's own translation units only and do not leak to its
# consumers.

if(TARGET nam_warnings)
    return()
endif()

option(NAM_WARNINGS_AS_ERRORS "Treat NAM compiler warnings as errors" ON)

add_library(nam_warnings INTERFACE)

set(_nam_gnu_like_warnings
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wold-style-cast
    -Wcast-qual
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
    -Wnull-dereference
    -Wunused
)

set(_nam_msvc_warnings
    /W4
    /permissive-
    /w14640  # Thread-unsafe static member initialization.
    /w14242  # Possible loss of data on assignment.
    /w14263  # Member function does not override any base class virtual.
    /w14265  # Class has virtual functions but a non-virtual destructor.
)

if(MSVC)
    target_compile_options(nam_warnings INTERFACE ${_nam_msvc_warnings})
    if(NAM_WARNINGS_AS_ERRORS)
        target_compile_options(nam_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(nam_warnings INTERFACE ${_nam_gnu_like_warnings})
    if(NAM_WARNINGS_AS_ERRORS)
        target_compile_options(nam_warnings INTERFACE -Werror)
    endif()
endif()
