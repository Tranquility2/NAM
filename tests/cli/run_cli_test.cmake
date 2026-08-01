# Portable CLI integration-test driver, run via `cmake -P`.
#
# It launches the built executable with stdin/stdout redirected (so the program
# is never attached to a TTY and takes its plain, ANSI-free path), then asserts:
#   * the process exit code equals EXPECT_CODE (when given);
#   * combined stdout+stderr contains EXPECT_CONTAINS (when given);
#   * combined output contains no ESC (0x1B) byte, when FORBID_ESC is set;
#   * two identical invocations produce byte-identical output, when
#     EXPECT_DETERMINISTIC is set.
#
# Definitions (all optional except EXE):
#   EXE                  path to the executable under test
#   ARGS                 ';'-separated argument list (CMake list)
#   INPUT_FILE           file piped to stdin
#   EXPECT_CODE          required integer exit code
#   EXPECT_CONTAINS      substring that must appear in the output
#   FORBID_ESC           when 1, the output must contain no ESC byte
#   EXPECT_DETERMINISTIC when 1, run twice and require identical output
#   TIMEOUT              seconds before a non-responsive child is killed (default 30)

if(NOT DEFINED EXE)
    message(FATAL_ERROR "run_cli_test: EXE is required")
endif()

if(NOT DEFINED TIMEOUT OR TIMEOUT STREQUAL "")
    set(TIMEOUT 30)
endif()

if(NOT DEFINED ARGS)
    set(ARGS "")
else()
    # Arguments arrive '|'-separated so their internal list stays intact through
    # add_test's ${ARGN} expansion (a ';' would be split there). Restore the
    # CMake list separator here so execute_process passes each as its own argv.
    string(REPLACE "|" ";" ARGS "${ARGS}")
endif()

# Build the execute_process keyword list, adding INPUT_FILE only when provided.
set(_input_kw "")
if(DEFINED INPUT_FILE AND NOT INPUT_FILE STREQUAL "")
    set(_input_kw INPUT_FILE "${INPUT_FILE}")
endif()

function(run_once out_var code_var)
    execute_process(
        COMMAND "${EXE}" ${ARGS}
        ${_input_kw}
        TIMEOUT ${TIMEOUT}
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err
        RESULT_VARIABLE _code)
    set(${out_var} "${_out}${_err}" PARENT_SCOPE)
    set(${code_var} "${_code}" PARENT_SCOPE)
endfunction()

run_once(output code)

# A child that stops responding (waiting on input it will never get, or blocked
# behind a crash dialog) reports a message rather than a number here. Fail loudly
# with whatever it managed to print, instead of letting CTest wait it out.
if(NOT code MATCHES "^-?[0-9]+$")
    message(FATAL_ERROR
        "the process did not finish: ${code}\n--- output so far ---\n${output}")
endif()

if(DEFINED EXPECT_CODE AND NOT code STREQUAL EXPECT_CODE)
    message(FATAL_ERROR
        "exit code ${code} != expected ${EXPECT_CODE}\n--- output ---\n${output}")
endif()

if(DEFINED EXPECT_CONTAINS)
    # EXPECT_CONTAINS may carry several required substrings, '|'-separated, so the
    # completed-run report path can assert its banners in one invocation.
    string(REPLACE "|" ";" _expect_list "${EXPECT_CONTAINS}")
    foreach(_needle IN LISTS _expect_list)
        string(FIND "${output}" "${_needle}" _pos)
        if(_pos EQUAL -1)
            message(FATAL_ERROR
                "output does not contain '${_needle}'\n--- output ---\n${output}")
        endif()
    endforeach()
endif()

if(FORBID_ESC)
    string(ASCII 27 _esc)
    string(FIND "${output}" "${_esc}" _esc_pos)
    if(NOT _esc_pos EQUAL -1)
        message(FATAL_ERROR "output unexpectedly contains an ESC (ANSI) byte at ${_esc_pos}")
    endif()
endif()

if(EXPECT_DETERMINISTIC)
    run_once(output2 code2)
    if(NOT output STREQUAL output2)
        message(FATAL_ERROR "output is not deterministic across identical runs")
    endif()
    if(NOT code STREQUAL code2)
        message(FATAL_ERROR "exit code is not deterministic across identical runs")
    endif()
endif()
