# Convenience wrapper around CMake. The real build system is CMake (see
# CMakeLists.txt and CMakePresets.json) — this Makefile just gives short
# commands for common tasks and never duplicates build logic.

BUILD_DIR       ?= build
BUILD_TYPE      ?= Release
JOBS            ?= $(shell nproc 2>/dev/null || echo 4)
CMAKE_GENERATOR ?= Unix Makefiles

# Dedicated build trees for the specialised tasks (all under build/, ignored).
TEST_DIR         = $(BUILD_DIR)/test
SANITIZE_DIR     = $(BUILD_DIR)/sanitize
MINGW_DIR        = $(BUILD_DIR)/mingw
MINGW_TOOLCHAIN  = cmake/toolchains/mingw-w64-x86_64.cmake
MINGW_EXE        = $(MINGW_DIR)/NAM_Console/nam_console.exe

# Fail-fast sanitizer runtime options (mirrors the asan-ubsan test preset).
ASAN_OPTIONS_RT  = halt_on_error=1:abort_on_error=1:detect_leaks=1:print_stacktrace=1
UBSAN_OPTIONS_RT = halt_on_error=1:print_stacktrace=1

# CMake option flags for each optional target.
CMAKE_OPTS_BASE = -S . -B $(BUILD_DIR) -G "$(CMAKE_GENERATOR)" \
                  -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

.PHONY: all console sdl sdl-test configure configure-sdl-test configure-sdl \
        build rebuild run run-sdl-test test sanitize mingw mingw-test \
        clean distclean help

# Default target: build the console game.
all: console

help:
	@echo "Common targets:"
	@echo "  make               Build the console game (default)"
	@echo "  make console       Build only NAM_Console"
	@echo "  make sdl           Build NAM_SDL (future graphical version)"
	@echo "  make sdl-test      Build the SDL sandbox (downloads SDL2 on 1st run)"
	@echo "  make run           Run the console game"
	@echo "  make test          Configure + build + run the CTest suite"
	@echo "  make sanitize      Build with ASan/UBSan (fail-fast) and run the tests"
	@echo "  make mingw         Cross-compile the Windows .exe with MinGW-w64"
	@echo "  make mingw-test    Cross-compile, then run the .exe under wine if present"
	@echo "  make rebuild       Wipe build/ and rebuild from scratch"
	@echo "  make clean         Remove build artifacts (keeps CMake cache)"
	@echo "  make distclean     Remove the entire build/ directory"
	@echo ""
	@echo "Reproducible presets (need Ninja): cmake --preset {release|debug|asan-ubsan}"
	@echo "  then: cmake --build --preset <name> && ctest --preset <name>"
	@echo ""
	@echo "Overridable variables:"
	@echo "  BUILD_DIR=$(BUILD_DIR)   BUILD_TYPE=$(BUILD_TYPE)   JOBS=$(JOBS)"

# ---- configure -------------------------------------------------------------

configure:
	cmake $(CMAKE_OPTS_BASE) -DNAM_BUILD_CONSOLE=ON

configure-sdl:
	cmake $(CMAKE_OPTS_BASE) -DNAM_BUILD_CONSOLE=ON -DNAM_BUILD_SDL=ON

configure-sdl-test:
	cmake $(CMAKE_OPTS_BASE) -DNAM_BUILD_CONSOLE=ON -DNAM_BUILD_SDL_TEST=ON

# ---- build -----------------------------------------------------------------

console: configure
	cmake --build $(BUILD_DIR) --target nam_console -j$(JOBS)

sdl: configure-sdl
	cmake --build $(BUILD_DIR) --target nam_sdl -j$(JOBS)

sdl-test: configure-sdl-test
	cmake --build $(BUILD_DIR) --target sdl_test -j$(JOBS)

build: configure
	cmake --build $(BUILD_DIR) -j$(JOBS)

rebuild: distclean build

# ---- run -------------------------------------------------------------------

run: console
	./$(BUILD_DIR)/NAM_Console/nam_console

run-sdl-test: sdl-test
	cd $(BUILD_DIR)/SDL_test && ./sdl_test

# ---- tests -----------------------------------------------------------------

# Configure a Debug tree with the test suite enabled, build it, and run CTest.
test:
	cmake -S . -B $(TEST_DIR) -G "$(CMAKE_GENERATOR)" \
	      -DCMAKE_BUILD_TYPE=Debug -DNAM_BUILD_TESTS=ON
	cmake --build $(TEST_DIR) -j$(JOBS)
	cd $(TEST_DIR) && ctest --output-on-failure

# Build the tests with AddressSanitizer + UndefinedBehaviorSanitizer and run
# them fail-fast, so any sanitizer finding aborts and fails the suite.
sanitize:
	cmake -S . -B $(SANITIZE_DIR) -G "$(CMAKE_GENERATOR)" \
	      -DCMAKE_BUILD_TYPE=Debug -DNAM_BUILD_TESTS=ON \
	      -DNAM_SANITIZE="address;undefined"
	cmake --build $(SANITIZE_DIR) -j$(JOBS)
	cd $(SANITIZE_DIR) && \
	  ASAN_OPTIONS=$(ASAN_OPTIONS_RT) UBSAN_OPTIONS=$(UBSAN_OPTIONS_RT) \
	  ctest --output-on-failure

# ---- Windows cross-compilation (from Linux) --------------------------------

# Cross-compile the console executable for Windows and confirm it is a PE image.
mingw:
	cmake -S . -B $(MINGW_DIR) -G "$(CMAKE_GENERATOR)" \
	      -DCMAKE_TOOLCHAIN_FILE=$(MINGW_TOOLCHAIN) \
	      -DCMAKE_BUILD_TYPE=Release -DNAM_BUILD_TESTS=OFF
	cmake --build $(MINGW_DIR) --target nam_console -j$(JOBS)
	@file $(MINGW_EXE) 2>/dev/null || echo "Built $(MINGW_EXE)"

# Run the cross-built .exe under wine when available (otherwise skip cleanly).
mingw-test: mingw
	@if command -v wine >/dev/null 2>&1; then \
	  echo "Running the cross-built .exe under wine (plain mode)..."; \
	  printf 'q\n' | wine $(MINGW_EXE) --plain; \
	else \
	  echo "wine is not installed; skipping execution of $(MINGW_EXE)."; \
	  echo "The cross-build itself succeeded (see 'make mingw')."; \
	fi

# ---- clean -----------------------------------------------------------------

clean:
	@if [ -d $(BUILD_DIR) ]; then cmake --build $(BUILD_DIR) --target clean; fi

distclean:
	rm -rf $(BUILD_DIR)
