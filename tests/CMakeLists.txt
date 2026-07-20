# Unit and integration tests.
#
# A pinned, lightweight framework (doctest) is fetched reproducibly with
# FetchContent. doctest is header-only and compiles into each test binary; it is
# never subject to the nam_warnings policy (its include is added as SYSTEM), so
# warnings-as-errors applies only to our own code.
#
# Tests link nam_console_lib, which is the whole frontend minus main(), so every
# behaviour — parsing, movement, rendering, the input decoder, settings, the
# HUD, message mapping, and the plain/interactive app loop — is exercised
# without a real terminal. CLI exit codes and "no ANSI when redirected" are
# checked by running the real nam_console binary through a portable CMake driver
# script (no live TTY required).

if(NOT TARGET nam_console_lib)
    message(FATAL_ERROR "NAM tests require the console frontend; configure with -DNAM_BUILD_CONSOLE=ON")
endif()

include(FetchContent)

# Keep doctest's own tests/install out of our build.
set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE BOOL "" FORCE)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.12  # Pinned for reproducibility and CMake 4 compatibility.
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(doctest)

# The discovery helper registers each doctest TEST_CASE as its own CTest test.
include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)

# doctest headers as SYSTEM so -Werror never trips on the framework.
add_library(nam_doctest INTERFACE)
target_include_directories(nam_doctest SYSTEM INTERFACE ${doctest_SOURCE_DIR})

# One shared translation unit provides doctest's implementation and main().
add_library(nam_doctest_main STATIC doctest_main.cpp)
target_link_libraries(nam_doctest_main PUBLIC nam_doctest)

set(NAM_FIXTURES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

# Build a doctest executable from SOURCES and register its cases with the given
# CTest LABELS. NAM_FIXTURES_DIR is passed as a compile definition so tests open
# fixtures by absolute path regardless of the working directory.
function(nam_add_unit_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LABELS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE nam_console_lib nam_doctest_main nam_warnings)
    target_compile_definitions(${name} PRIVATE NAM_FIXTURES_DIR="${NAM_FIXTURES_DIR}")
    # ADD_LABELS 1 turns each test's TEST_SUITE name into a CTest label
    # (parser/game/input/console); PROPERTIES LABELS adds the shared "unit" tag.
    # Note: ADD_LABELS resolves a case's suite by name, so TEST_CASE names must
    # avoid doctest filter metacharacters (',' ';' '*' '?') or the suite label
    # is skipped for that case.
    doctest_discover_tests(${name} ADD_LABELS 1 PROPERTIES LABELS "unit")
endfunction()

nam_add_unit_test(nam_parser_tests
    SOURCES test_map_parser.cpp test_terrain_coords.cpp)

nam_add_unit_test(nam_game_tests
    SOURCES test_movement.cpp test_rendering.cpp test_determinism.cpp
            test_pcg32.cpp)

nam_add_unit_test(nam_input_tests
    SOURCES test_input_decoder.cpp)

nam_add_unit_test(nam_console_tests
    SOURCES test_settings.cpp test_renderer.cpp test_messages.cpp
            test_app_state.cpp test_console_app.cpp)

# --- CLI integration tests --------------------------------------------------
#
# These run the real executable with stdin/stdout redirected (never a TTY), so
# the app takes its plain, ANSI-free path. The driver script asserts the exit
# code and, where relevant, that no ESC byte was emitted.
if(TARGET nam_console)
    set(_cli_driver ${CMAKE_CURRENT_SOURCE_DIR}/cli/run_cli_test.cmake)
    set(_quit_stdin ${CMAKE_CURRENT_SOURCE_DIR}/cli/stdin_quit.txt)
    set(_moves_stdin ${CMAKE_CURRENT_SOURCE_DIR}/cli/stdin_moves.txt)

    # add a CLI test: name plus driver options forwarded as -D definitions.
    function(nam_add_cli_test name)
        add_test(
            NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                    -DEXE=$<TARGET_FILE:nam_console>
                    ${ARGN}
                    -P ${_cli_driver})
        set_tests_properties(${name} PROPERTIES LABELS "cli;integration")
    endfunction()

    nam_add_cli_test(cli_help
        "-DARGS=--help" -DEXPECT_CODE=0 "-DEXPECT_CONTAINS=Usage:" -DFORBID_ESC=1)
    nam_add_cli_test(cli_help_short
        "-DARGS=-h" -DEXPECT_CODE=0 "-DEXPECT_CONTAINS=Usage:")
    nam_add_cli_test(cli_version
        "-DARGS=--version" -DEXPECT_CODE=0 "-DEXPECT_CONTAINS=nam_console 0.2.0" -DFORBID_ESC=1)
    nam_add_cli_test(cli_unknown_option
        "-DARGS=--definitely-not-an-option" -DEXPECT_CODE=2 "-DEXPECT_CONTAINS=unknown option")
    nam_add_cli_test(cli_conflicting_map
        "-DARGS=--map|a.map|b.map" -DEXPECT_CODE=2 "-DEXPECT_CONTAINS=not both")
    nam_add_cli_test(cli_missing_map_value
        "-DARGS=--map" -DEXPECT_CODE=2 "-DEXPECT_CONTAINS=requires a path")
    nam_add_cli_test(cli_missing_map_file
        "-DARGS=--map|${NAM_FIXTURES_DIR}/does-not-exist.map" -DEXPECT_CODE=1
        "-DEXPECT_CONTAINS=map")
    nam_add_cli_test(cli_malformed_map
        "-DARGS=--map|${NAM_FIXTURES_DIR}/malformed-header.map" -DEXPECT_CODE=1)
    nam_add_cli_test(cli_unknown_symbol_map
        "-DARGS=--map|${NAM_FIXTURES_DIR}/unknown-terrain.map" -DEXPECT_CODE=1
        "-DEXPECT_CONTAINS=symbol")
    nam_add_cli_test(cli_run_builtin_plain
        "-DARGS=--plain" -DINPUT_FILE=${_quit_stdin} -DEXPECT_CODE=0
        "-DEXPECT_CONTAINS=Goodbye" -DFORBID_ESC=1)
    nam_add_cli_test(cli_run_builtin_redirected
        -DINPUT_FILE=${_quit_stdin} -DEXPECT_CODE=0 -DFORBID_ESC=1)
    nam_add_cli_test(cli_run_valid_map
        "-DARGS=--map|${NAM_FIXTURES_DIR}/valid-lf.map" -DINPUT_FILE=${_moves_stdin}
        -DEXPECT_CODE=0 -DFORBID_ESC=1)
    nam_add_cli_test(cli_run_deterministic
        "-DARGS=--map|${NAM_FIXTURES_DIR}/valid-lf.map" -DINPUT_FILE=${_moves_stdin}
        -DEXPECT_CODE=0 -DEXPECT_DETERMINISTIC=1)
endif()
