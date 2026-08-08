#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "coordinates.h"
#include "direction.h"
#include "expedition.h"
#include "game_state.h"
#include "messages.h"
#include "renderer.h"

#include "scripted_walk.h"

// Viewport readability, checked on levels the generator really produced.
//
// The renderer's own size tests use synthetic open maps: no fog, no placed
// features, no objective overlay, no landmark name in the objective line. Those
// are exactly the things whose width is not known until a level exists, and
// X-Large is 51x27 with four discoveries, five vantage points, a landmark and an
// exit to overlay. A frame that is bounded on an empty 120x60 map can still be
// overflowed by a long generated landmark name.
//
// The claim is the one a player depends on: whatever the terminal, the frame fits
// it exactly, the actor is on screen, and the objective line is still there to
// read.

using namespace nam::console;

namespace {

constexpr std::array<LevelTier, 4> kTiers{LevelTier::small, LevelTier::medium, LevelTier::large,
                                          LevelTier::x_large};

// Sizes a player might really have, from a cramped split pane up to a full
// screen, including the exact X-Large map size and the 80x24 default.
constexpr std::array<TerminalSize, 9> kSizes{{{40, 12},
                                              {51, 27},
                                              {60, 20},
                                              {80, 24},
                                              {100, 40},
                                              {120, 30},
                                              {132, 43},
                                              {30, 10},
                                              {20, 8}}};

[[nodiscard]] std::string strip_ansi(const std::string& text) {
    std::string visible;
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] == '\x1b') {
            while (index < text.size() && text[index] != 'm') ++index;
            if (index < text.size()) ++index;
            continue;
        }
        visible += text[index];
        ++index;
    }
    return visible;
}

[[nodiscard]] RenderInput input_for(const GameState& state) {
    RenderInput input;
    input.map = &state.map();
    input.visibility = &state.visibility();
    input.actor = state.actor_position();
    input.terrain = state.map().terrain_at(input.actor);
    input.objective = &state.objective();
    return input;
}

[[nodiscard]] bool frame_contains(const Frame& frame, const std::string& needle) {
    for (const std::string& row : frame) {
        if (strip_ansi(row).find(needle) != std::string::npos) return true;
    }
    return false;
}

// The renderer drops to a compact layout on a small terminal, where the objective
// line is abbreviated. Either wording satisfies the claim, which is that the
// player can still read what they are being asked to do.
[[nodiscard]] bool shows_objective(const Frame& frame) {
    return frame_contains(frame, "Objective:") || frame_contains(frame, "Goal:");
}

// Whether the actor is visible *on the map*, which is the only place it means the
// viewport followed them.
//
// Searching the whole frame for the actor glyph does not work: the legend row
// reads "O you  * goal ...", so it matches even when the map region shows no
// actor at all. This was not a hypothetical - pinning the viewport origin to zero
// left an earlier version of this gate passing every one of its assertions.
//
// A map row is identified by its alphabet: terrain symbols and overlay glyphs
// only. Every HUD row carries prose, so no HUD row can be mistaken for one.
[[nodiscard]] bool shows_actor_on_map(const Frame& frame) {
    static const std::string map_alphabet = ".@~x^=|#&O*?+ ";
    for (const std::string& row : frame) {
        const std::string visible = strip_ansi(row);
        if (visible.find_first_not_of(map_alphabet) != std::string::npos) continue;
        if (visible.find(actor_glyph) != std::string::npos) return true;
    }
    return false;
}

// Whether a frame row belongs to the map region. A map row is identified by its
// alphabet: terrain symbols and overlay glyphs only. Every HUD row carries prose,
// so no HUD row can be mistaken for one.
[[nodiscard]] bool is_map_row(const std::string& visible) {
    static const std::string map_alphabet = ".@~x^=|#&O*?+ ";
    return visible.find_first_not_of(map_alphabet) == std::string::npos;
}

[[nodiscard]] bool ends_with(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Whether a trimmed row is a prefix of some wording that stops exactly where a
// word ends. This is the difference between "Reached Ember Bough..." and
// "Reached Ember Bou...": both fit, only one is a sentence the player can read.
[[nodiscard]] bool cut_at_word_boundary(const std::string& trimmed,
                                        const std::vector<std::string>& forms) {
    const std::string kept = trimmed.substr(0, trimmed.size() - std::string(hud_trim_marker).size());
    if (kept.empty()) return false;
    for (const std::string& form : forms) {
        if (form.size() <= kept.size()) continue;
        if (form.compare(0, kept.size(), kept) != 0) continue;
        if (form[kept.size()] == ' ') return true;
    }
    return false;
}

}  // namespace

TEST_SUITE("viewport_readability") {

TEST_CASE("a real level of any tier renders inside any terminal it is given") {
    const Renderer renderer(RenderConfig{/*use_color=*/false, /*use_ansi=*/false,
                                         /*debug=*/false, /*emphasis=*/false});
    for (const LevelTier tier : kTiers) {
        for (std::uint64_t seed = 0; seed < 12u; ++seed) {
            GameState state = make_level_state(tier, seed);
            const RenderInput input = input_for(state);
            for (const TerminalSize size : kSizes) {
                const Frame frame = renderer.render(input, size);

                // Exactly the terminal, never one row or column more. A frame that
                // overflows scrolls the terminal and destroys the previous frame.
                REQUIRE(frame.size() == static_cast<std::size_t>(size.rows));
                for (const std::string& row : frame) {
                    CHECK(strip_ansi(row).size() <= static_cast<std::size_t>(size.columns));
                }

                // The player has to be able to see themselves and what they are
                // being asked to do. Both survive on the largest map in the
                // smallest window the renderer still draws a map in, which is why
                // there is no size threshold here: every size in the sweep is at
                // or above the renderer's absolute minimum, so every one of them
                // owes the player a map.
                CHECK(shows_actor_on_map(frame));
                CHECK(shows_objective(frame));
            }
        }
    }
}

TEST_CASE("the viewport keeps up with the actor wherever a real level leads") {
    // A stationary actor sits at the centre spawn, which is the easiest cell in
    // the map to keep on screen. The interesting case is a player who has walked
    // to a corner of an X-Large map, where the viewport has to have scrolled.
    const Renderer renderer(RenderConfig{/*use_color=*/false, /*use_ansi=*/false,
                                         /*debug=*/false, /*emphasis=*/false});
    for (const LevelTier tier : kTiers) {
        for (std::uint64_t seed = 0; seed < 6u; ++seed) {
            GameState state = make_level_state(tier, seed);

            // Walk to the exit, which the template always puts in a corner region,
            // and render at every step so no intermediate frame is missed.
            for (int step = 0; step < 400; ++step) {
                const std::vector<Coordinates> path =
                    shortest_path(state.map(), state.actor_position(), state.objective().exit_cell);
                if (path.size() < 2u) break;
                const Coordinates delta{path[1].x - path[0].x, path[1].y - path[0].y};
                const std::optional<Direction> direction = direction_of(delta);
                REQUIRE(direction.has_value());
                static_cast<void>(state.move(*direction));

                const RenderInput input = input_for(state);
                for (const TerminalSize size : kSizes) {
                    const Frame frame = renderer.render(input, size);
                    REQUIRE(frame.size() == static_cast<std::size_t>(size.rows));
                    for (const std::string& row : frame) {
                        CHECK(strip_ansi(row).size() <= static_cast<std::size_t>(size.columns));
                    }
                    CHECK(shows_actor_on_map(frame));
                }
            }
        }
    }
}

TEST_CASE("a narrow terminal shortens the HUD instead of cutting it off") {
    // The failure this gates is not hypothetical. The HUD status line is 67 to 72
    // columns wide and the legend is 69, while the standard layout starts at 34
    // columns, so every terminal between the two - which is every split pane -
    // used to be shown those lines with their ends sliced off mid-word: "Sight 3
    // (open " and a legend that stopped at "Bl". Nothing in the suite noticed,
    // because fitting the terminal was checked and being readable was not.
    //
    // The claim now is stronger and exact: every HUD row a player is shown is a
    // *complete* wording the renderer chose, not a longer one clipped. The single
    // stated exception is the latest-event message, which is free prose with no
    // authored short form; it is trimmed at a word boundary and says so.
    const Renderer renderer(RenderConfig{/*use_color=*/false, /*use_ansi=*/false,
                                         /*debug=*/false, /*emphasis=*/false});
    for (const LevelTier tier : kTiers) {
        for (std::uint64_t seed = 0; seed < 8u; ++seed) {
            GameState state = make_level_state(tier, seed);
            // Two messages: the ordinary per-step wording, and the longest one the
            // game really produces - a discovery announcement carrying a generated
            // landmark name. The long one is what forces the word-trimming
            // backstop, so both the ladder and the fallback are exercised.
            const std::string messages[] = {"Moved onto shallow water. Sight 4.",
                                            describe_landmark_discovered(state.objective().name)};
            for (const std::string& message : messages) {
                RenderInput input = input_for(state);
                input.message = message;

                const std::vector<std::string> forms = hud_line_forms(input);
                for (const TerminalSize size : kSizes) {
                    const Frame frame = renderer.render(input, size);
                    for (const std::string& row : frame) {
                        const std::string visible = strip_ansi(row);
                        if (visible.empty() || is_map_row(visible)) continue;

                        INFO("size ", size.columns, "x", size.rows, " row: ", visible);
                        if (std::find(forms.begin(), forms.end(), visible) != forms.end()) {
                            continue;  // A complete authored wording: nothing lost.
                        }
                        // Otherwise it must be the stated exception, and it must
                        // have been cut where a word ended rather than inside one.
                        REQUIRE(ends_with(visible, hud_trim_marker));
                        CHECK(cut_at_word_boundary(visible, forms));
                    }
                }
            }
        }
    }
}

}  // TEST_SUITE("viewport_readability")
