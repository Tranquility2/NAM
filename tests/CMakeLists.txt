# Unit, acceptance and integration tests.
#
# The tree mirrors the architecture rather than the test framework:
#
#   core/        rules that must hold with no frontend present; links nam_core
#                only, so a core test that reaches for console code fails to
#                link and the frontend-neutrality rule is enforced, not assumed.
#   console/     frontend behaviour; links nam_console_lib, which is the whole
#                console minus main(), so settings, input decoding, rendering,
#                messages, the HUD and both app loops are exercised without a
#                real terminal.
#   acceptance/  slow property and gate sweeps over many seeds. Separated so the
#                fast suites stay fast and CI can run the two tiers apart.
#   cli/         the real nam_console executable driven end to end.
#   support/     header-only helpers shared by the suites above (never by the
#                shipped code), on every test target's include path.
#
# Every case carries three kinds of label: its module (from the doctest suite
# name, one per file), its layer (core/console/acceptance/cli), and its kind
# (unit/integration) or cost (slow). So `ctest -L objective` runs one module,
# `ctest -L core` one layer, and `ctest -LE slow` everything but the sweeps.
#
# A pinned, lightweight framework (doctest) is fetched reproducibly with
# FetchContent. doctest is header-only and compiles into each test binary; it is
# never subject to the nam_warnings policy (its include is added as SYSTEM), so
# warnings-as-errors applies only to our own code.

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

# Map fixtures are shared: the same files feed core parsing, console rendering
# and the CLI runs, so they stay in one place and are addressed by absolute path.
set(NAM_FIXTURES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

# Build a doctest executable from SOURCES against LINK, and register its cases
# with LABELS. NAM_FIXTURES_DIR is passed as a compile definition so tests open
# fixtures by absolute path regardless of the working directory.
#
# Labels arrive on two axes and need two mechanisms. doctest's ADD_LABELS gives
# the module axis (one suite per file), but it writes its own LABELS entry into
# the same set_tests_properties call as anything passed through PROPERTIES, and
# the duplicate key silently drops one of the two — which is why the old blanket
# "unit" label was never actually applied. The layer and kind labels are
# therefore applied by a companion CTest include file that walks the case list
# doctest publishes; a second set_tests_properties call adds to the labels
# already on a test rather than replacing them.
function(nam_add_test_executable name)
    cmake_parse_arguments(ARG "" "LINK" "SOURCES;LABELS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE ${ARG_LINK} nam_doctest_main nam_warnings)
    target_compile_definitions(${name} PRIVATE NAM_FIXTURES_DIR="${NAM_FIXTURES_DIR}")
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/support)
    # Note: ADD_LABELS resolves a case's suite by name, so TEST_CASE names must
    # avoid doctest filter metacharacters (',' ';' '*' '?') or the suite label
    # is skipped for that case.
    # TIMEOUT bounds a hung case: every case here runs in milliseconds, so a test
    # that reaches 60s is wedged, and without the bound CTest would wait out its
    # 1500s default — long enough to burn a whole CI job on one hang.
    doctest_discover_tests(${name} ADD_LABELS 1 PROPERTIES TIMEOUT 60)

    set(_extra_labels ${CMAKE_CURRENT_BINARY_DIR}/${name}_labels.cmake)
    file(WRITE ${_extra_labels}
         "foreach(_case IN LISTS ${name}_TESTS)\n"
         "    set_tests_properties(\"\${_case}\" PROPERTIES LABELS \"${ARG_LABELS}\")\n"
         "endforeach()\n")
    # Appended after doctest's own include file so the case list already exists.
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES ${_extra_labels})
endfunction()

add_subdirectory(core)
add_subdirectory(console)
add_subdirectory(acceptance)
add_subdirectory(cli)
