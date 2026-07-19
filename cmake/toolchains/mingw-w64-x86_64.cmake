# CMake toolchain for cross-compiling the Windows target from Linux with
# MinGW-w64 (x86_64). Use it to verify that the console frontend builds for
# Windows without a Windows machine:
#
#     cmake -S . -B build/mingw \
#           -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake \
#           -DNAM_BUILD_TESTS=OFF
#     cmake --build build/mingw --target nam_console
#
# The produced nam_console.exe is a PE32+ executable. This complements — it does
# not replace — the native MSVC CI job.
#
# The compiler prefix can be overridden for other MinGW installations:
#     -DMINGW_TARGET_PREFIX=i686-w64-mingw32   (32-bit, for example)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT DEFINED MINGW_TARGET_PREFIX)
    set(MINGW_TARGET_PREFIX x86_64-w64-mingw32)
endif()

set(CMAKE_C_COMPILER   ${MINGW_TARGET_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${MINGW_TARGET_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${MINGW_TARGET_PREFIX}-windres)

# Where to look for the target environment (libraries, headers).
set(CMAKE_FIND_ROOT_PATH /usr/${MINGW_TARGET_PREFIX})

# Search programs on the host; libraries and headers in the target root only.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Statically link the GCC/C++ runtimes so the resulting .exe is self-contained
# and can be launched (for example by wine) without shipping MinGW DLLs.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
