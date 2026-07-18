# Convenience wrapper around CMake. The real build system is CMake —
# this Makefile just gives you short commands for common tasks.

BUILD_DIR       ?= build
BUILD_TYPE      ?= Release
JOBS            ?= $(shell nproc 2>/dev/null || echo 4)
CMAKE_GENERATOR ?= Unix Makefiles

# CMake option flags for each optional target.
CMAKE_OPTS_BASE = -S . -B $(BUILD_DIR) -G "$(CMAKE_GENERATOR)" \
                  -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

.PHONY: all console sdl sdl-test configure configure-sdl-test configure-sdl \
        build rebuild run run-sdl-test clean distclean help

# Default target: build the console game.
all: console

help:
	@echo "Common targets:"
	@echo "  make               Build the console game (default)"
	@echo "  make console       Build only NAM_Console"
	@echo "  make sdl           Build NAM_SDL (future graphical version)"
	@echo "  make sdl-test      Build the SDL sandbox (downloads SDL2 on 1st run)"
	@echo "  make run           Run the console game"
	@echo "  make run-sdl-test  Run the SDL sandbox"
	@echo "  make rebuild       Wipe build/ and rebuild from scratch"
	@echo "  make clean         Remove build artifacts (keeps CMake cache)"
	@echo "  make distclean     Remove the entire build/ directory"
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

# ---- clean -----------------------------------------------------------------

clean:
	@if [ -d $(BUILD_DIR) ]; then cmake --build $(BUILD_DIR) --target clean; fi

distclean:
	rm -rf $(BUILD_DIR)
