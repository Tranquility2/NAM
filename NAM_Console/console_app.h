#pragma once

#include <istream>
#include <optional>
#include <ostream>
#include <string>

#include "app_state.h"
#include "direction.h"
#include "game_state.h"
#include "input.h"
#include "renderer.h"
#include "settings.h"
#include "terminal.h"

namespace nam::console {

// Drives one play session. The app owns the mutable GameState and the bounded
// HUD, translates semantic input events into core moves, and asks a Renderer for
// frames. It knows nothing about byte transports or platform APIs, so the same
// object serves both the interactive and the plain-text paths.
class ConsoleApp {
public:
    ConsoleApp(GameState state, Settings settings);

    // Event-driven interactive loop over a raw terminal session. Blocks on input
    // (idle CPU is ~zero), redraws after each event and on resize, and returns a
    // process exit code. Terminal restoration is guaranteed by the session RAII.
    [[nodiscard]] int run_interactive(TerminalSession& session);

    // Line-oriented fallback for non-terminals or --plain. Reads whole commands,
    // never touches raw mode or the cursor, and stays readable when redirected.
    [[nodiscard]] int run_plain(std::istream& input, std::ostream& output);

    // The final latest-event message, shown once after interactive teardown.
    [[nodiscard]] const std::string& final_message() const noexcept { return hud_.message(); }

private:
    [[nodiscard]] RenderInput make_input(bool emphasize) const;
    void apply_move(Direction direction, bool& emphasize);

    GameState state_;
    Settings settings_;
    Hud hud_;
};

// Map a semantic event to a movement direction, or std::nullopt when the event
// is not a movement command. Exposed for direct testing.
[[nodiscard]] std::optional<Direction> direction_for(const KeyEvent& event) noexcept;

// Whether an event asks to quit (Escape, or 'q'). Exposed for testing.
[[nodiscard]] bool is_quit_event(const KeyEvent& event) noexcept;

// Top-level orchestration: choose interactive vs plain mode from settings and
// platform capability, create the session if needed, and run. Returns the
// process exit code (0 normal; 2 if interactive init fails with no fallback).
[[nodiscard]] int run(GameState state, Settings settings, const Environment& environment);

}  // namespace nam::console
