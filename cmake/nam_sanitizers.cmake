# nam_sanitizers: translate the NAM_SANITIZE cache variable into compile/link
# flags applied project-wide.
#
# NAM_SANITIZE is a semicolon-separated list, for example "address;undefined".
# The asan-ubsan / msvc-asan presets set it; a normal build leaves it empty and
# this module is a no-op.
#
# Flags are added with add_compile_options/add_link_options so every target
# defined after this module is included is instrumented consistently (our code
# and the header-only doctest compiled into the test binaries). SDL is never
# fetched in sanitizer builds, so there is nothing heavy to instrument.
#
# Fail-fast: -fno-sanitize-recover=all makes the first undefined-behaviour or
# address error abort the process, so a sanitizer finding always fails the test
# run instead of merely printing and continuing.

set(NAM_SANITIZE "" CACHE STRING
    "Semicolon-separated sanitizers to enable project-wide (e.g. address;undefined)")

if(NOT NAM_SANITIZE)
    return()
endif()

message(STATUS "NAM sanitizers enabled: ${NAM_SANITIZE}")

if(MSVC)
    # MSVC supports AddressSanitizer only; UBSan and leak detection are silently
    # dropped from the list. The runtime is linked automatically by cl/link.
    list(FIND NAM_SANITIZE "address" _nam_has_asan)
    if(_nam_has_asan GREATER -1)
        add_compile_options(/fsanitize=address /Zi)
        # ASan is incompatible with the /RTC run-time checks CMake adds in Debug.
        # include() runs in the including scope, so a plain set() updates the
        # root-scope flags that subdirectories inherit (no PARENT_SCOPE needed).
        string(REGEX REPLACE "/RTC[1csu]*" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        string(REGEX REPLACE "/RTC[1csu]*" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    endif()
else()
    string(REPLACE ";" "," _nam_san_csv "${NAM_SANITIZE}")
    add_compile_options(
        -fsanitize=${_nam_san_csv}
        -fno-sanitize-recover=all
        -fno-omit-frame-pointer
        -g
    )
    add_link_options(-fsanitize=${_nam_san_csv})
endif()
