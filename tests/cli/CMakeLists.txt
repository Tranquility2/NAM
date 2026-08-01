# CLI integration tests.
#
# These run the real executable with stdin/stdout redirected (never a TTY), so
# the app takes its plain, ANSI-free path. The driver script asserts the exit
# code and, where relevant, that no ESC byte was emitted.
if(NOT TARGET nam_console)
    return()
endif()

set(_cli_driver ${CMAKE_CURRENT_SOURCE_DIR}/run_cli_test.cmake)
set(_quit_stdin ${CMAKE_CURRENT_SOURCE_DIR}/stdin_quit.txt)
set(_moves_stdin ${CMAKE_CURRENT_SOURCE_DIR}/stdin_moves.txt)
set(_level_stdin ${CMAKE_CURRENT_SOURCE_DIR}/stdin_level.txt)
set(_wander_stdin ${CMAKE_CURRENT_SOURCE_DIR}/stdin_wander.txt)
set(_phase0_stdin ${CMAKE_CURRENT_SOURCE_DIR}/stdin_phase0_level.txt)

# add a CLI test: name plus driver options forwarded as -D definitions. The
# TIMEOUT matters more here than for the unit suites: these launch the real
# executable with stdin redirected, so a frontend that stops consuming input
# blocks forever instead of failing.
function(nam_add_cli_test name)
    add_test(
        NAME ${name}
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:nam_console>
                ${ARGN}
                -P ${_cli_driver})
    set_tests_properties(${name} PROPERTIES LABELS "cli;integration" TIMEOUT 60)
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

# A scripted expedition on a dedicated corridor map walks from spawn through
# the landmark to the exit, so the real executable completes the objective and
# prints the final report, then exits 0 on the end-of-input acknowledgement
# with the report banners present and no ANSI when redirected. The run is also
# deterministic across identical runs.
nam_add_cli_test(cli_run_level_complete
    "-DARGS=--map|${NAM_FIXTURES_DIR}/level-corridor.map" -DINPUT_FILE=${_level_stdin}
    -DEXPECT_CODE=0
    "-DEXPECT_CONTAINS=EXPEDITION REPORT|ROUTE MAP|EXPEDITION JOURNAL|Score:|Discoveries:|Route:|Explored:"
    -DFORBID_ESC=1)
nam_add_cli_test(cli_run_level_deterministic
    "-DARGS=--map|${NAM_FIXTURES_DIR}/level-corridor.map" -DINPUT_FILE=${_level_stdin}
    -DEXPECT_CODE=0 -DEXPECT_DETERMINISTIC=1)

# The same corridor driven by a long wandering script: no meter can refuse a
# step, so the real executable keeps travelling, stays ANSI-free when
# redirected, and is deterministic across identical runs.
nam_add_cli_test(cli_run_level_wander
    "-DARGS=--map|${NAM_FIXTURES_DIR}/level-corridor.map" -DINPUT_FILE=${_wander_stdin}
    -DEXPECT_CODE=0
    "-DEXPECT_CONTAINS=Moved onto|Sight:|Objective:"
    -DFORBID_ESC=1)
nam_add_cli_test(cli_run_level_wander_deterministic
    "-DARGS=--map|${NAM_FIXTURES_DIR}/level-corridor.map" -DINPUT_FILE=${_wander_stdin}
    -DEXPECT_CODE=0 -DEXPECT_DETERMINISTIC=1)

# The Phase 0 exit condition end to end: the handcrafted serpentine level is
# completed from entry to exit by the real executable using movement alone. The
# script crosses hill, water, fields and mountain bands without a single
# blocked move, and the run ends on the concise report and journal.
nam_add_cli_test(cli_run_phase0_level
    "-DARGS=--map|${NAM_FIXTURES_DIR}/phase0-level.map" -DINPUT_FILE=${_phase0_stdin}
    -DEXPECT_CODE=0
    "-DEXPECT_CONTAINS=Result: level complete.|Route: 46 moves|0 blocked|Explored: 55 / 55 cells|Sighted Moon Way Landmark|Reached the exit after Moon Way Landmark"
    -DFORBID_ESC=1)
nam_add_cli_test(cli_run_phase0_level_deterministic
    "-DARGS=--map|${NAM_FIXTURES_DIR}/phase0-level.map" -DINPUT_FILE=${_phase0_stdin}
    -DEXPECT_CODE=0 -DEXPECT_DETERMINISTIC=1)

# --- seeded expedition integration -------------------------------------
#
# Exercise the real executable's seeded path: a plain seeded session prints
# the safely escaped seed and no ANSI, is deterministic across runs, and the
# usage errors (map conflict, overlong seed) exit 2. An empty seed is valid.
nam_add_cli_test(cli_seed_plain
    "-DARGS=--seed=glass-river|--plain" -DINPUT_FILE=${_quit_stdin} -DEXPECT_CODE=0
    "-DEXPECT_CONTAINS=Expedition seed:" -DFORBID_ESC=1)
nam_add_cli_test(cli_seed_deterministic
    "-DARGS=--seed=glass-river|--plain" -DINPUT_FILE=${_quit_stdin} -DEXPECT_CODE=0
    -DEXPECT_DETERMINISTIC=1)
nam_add_cli_test(cli_seed_empty
    "-DARGS=--seed=|--plain" -DINPUT_FILE=${_quit_stdin} -DEXPECT_CODE=0
    "-DEXPECT_CONTAINS=Expedition seed" -DFORBID_ESC=1)
nam_add_cli_test(cli_seed_map_conflict
    "-DARGS=--seed=glass-river|--map|${NAM_FIXTURES_DIR}/valid-lf.map" -DEXPECT_CODE=2
    "-DEXPECT_CONTAINS=not both")
string(REPEAT "a" 129 _overlong_seed)
nam_add_cli_test(cli_seed_overlong
    "-DARGS=--seed=${_overlong_seed}" -DEXPECT_CODE=2 "-DEXPECT_CONTAINS=128 bytes")

# --- numeric seed replay integration -----------------------------------
#
# The numeric-seed forms exercise the strict decimal parse and the direct
# generation path: boundary values are accepted, the world is deterministic,
# and every malformed/overflow/repeat/conflict case exits 2.
nam_add_cli_test(cli_seed_number_min
    "-DARGS=--seed-number=0|--plain" -DINPUT_FILE=${_quit_stdin} -DEXPECT_CODE=0
    "-DEXPECT_CONTAINS=Expedition seed number: 0" -DFORBID_ESC=1)
nam_add_cli_test(cli_seed_number_max
    "-DARGS=--seed-number|18446744073709551615|--plain" -DINPUT_FILE=${_quit_stdin}
    -DEXPECT_CODE=0 "-DEXPECT_CONTAINS=Expedition seed number: 18446744073709551615"
    -DFORBID_ESC=1)
nam_add_cli_test(cli_seed_number_deterministic
    "-DARGS=--seed-number=42|--plain" -DINPUT_FILE=${_quit_stdin} -DEXPECT_CODE=0
    -DEXPECT_DETERMINISTIC=1)
# The decimal hash of "glass-river" reproduces the same expedition as
# --seed glass-river (verified by the settings unit tests); here it must at
# least generate and report its numeric identity.
nam_add_cli_test(cli_seed_number_equivalent
    "-DARGS=--seed-number=1099592901251399996|--plain" -DINPUT_FILE=${_quit_stdin}
    -DEXPECT_CODE=0 "-DEXPECT_CONTAINS=Expedition seed number: 1099592901251399996"
    -DFORBID_ESC=1)
nam_add_cli_test(cli_seed_number_malformed
    "-DARGS=--seed-number=12x" -DEXPECT_CODE=2 "-DEXPECT_CONTAINS=decimal integer")
nam_add_cli_test(cli_seed_number_negative
    "-DARGS=--seed-number|-5" -DEXPECT_CODE=2 "-DEXPECT_CONTAINS=decimal integer")
nam_add_cli_test(cli_seed_number_overflow
    "-DARGS=--seed-number=18446744073709551616" -DEXPECT_CODE=2
    "-DEXPECT_CONTAINS=decimal integer")
nam_add_cli_test(cli_seed_number_repeat
    "-DARGS=--seed-number=1|--seed-number=2" -DEXPECT_CODE=2
    "-DEXPECT_CONTAINS=more than once")
nam_add_cli_test(cli_seed_number_conflict_seed
    "-DARGS=--seed-number=1|--seed=a" -DEXPECT_CODE=2 "-DEXPECT_CONTAINS=not both")
nam_add_cli_test(cli_seed_number_conflict_map
    "-DARGS=--seed-number=1|--map|${NAM_FIXTURES_DIR}/valid-lf.map" -DEXPECT_CODE=2
    "-DEXPECT_CONTAINS=not both")
