#include "renderer.h"

#include <algorithm>
#include <array>
#include <string>

#include "messages.h"

namespace nam::console {

namespace {

// --- Layout thresholds ------------------------------------------------------

constexpr int fallback_columns = 80;
constexpr int fallback_rows = 24;

constexpr int absolute_min_columns = 12;
constexpr int absolute_min_rows = 3;

constexpr int standard_min_columns = 34;
constexpr int standard_min_map_rows = 3;

// --- Colour helpers ---------------------------------------------------------

constexpr int color_actor = 93;  // Bright yellow.

[[nodiscard]] std::string sgr(int code) {
    return "\033[" + std::to_string(code) + "m";
}

[[nodiscard]] int color_for(Terrain terrain) noexcept {
    switch (terrain) {
        case Terrain::open:            return 90;  // Dim grey.
        case Terrain::mountain:        return 33;  // Yellow.
        case Terrain::water:           return 36;  // Cyan.
        case Terrain::fields:          return 32;  // Green.
        case Terrain::hill:            return 35;  // Magenta.
        case Terrain::wall_horizontal:
        case Terrain::wall_vertical:   return 37;  // White.
    }
    return 37;
}

// --- Small string utilities -------------------------------------------------

[[nodiscard]] std::string fit_plain(std::string text, int columns) {
    if (columns <= 0) {
        return std::string();
    }
    if (text.size() > static_cast<std::size_t>(columns)) {
        text.resize(static_cast<std::size_t>(columns));
    }
    return text;
}

[[nodiscard]] std::string center_plain(const std::string& text, int columns) {
    std::string trimmed = fit_plain(text, columns);
    const int pad = (columns - static_cast<int>(trimmed.size())) / 2;
    return std::string(static_cast<std::size_t>(std::max(0, pad)), ' ') + trimmed;
}

// --- Map viewport -----------------------------------------------------------

// Top-left cell of a viewport of `view` cells that keeps `actor` centred while
// staying inside [0, extent). Guarantees a non-negative origin.
[[nodiscard]] int viewport_origin(int actor, int extent, int view) {
    if (view >= extent) {
        return 0;
    }
    int origin = actor - view / 2;
    origin = std::max(0, origin);
    origin = std::min(origin, extent - view);
    return origin;
}

// Render one map row [x0, x0+cols) at row y, honoring per-cell exploration
// state. The renderer branches on CellVisibility *before* reading terrain so a
// hidden cell never leaks its glyph or colour:
//   * unexplored -> a single blank space, no style.
//   * remembered -> the terrain glyph, dimmed (ESC[2m) whenever ANSI is enabled,
//     even if colour is disabled.
//   * visible    -> the terrain glyph with its normal colour mapping.
//   * the actor  -> the existing actor styling, always drawn as actor_glyph.
// ANSI style transitions explicitly reset before switching kinds so dim or
// colour state cannot leak into adjacent cells, padding, HUD text, or later
// rows; a styled row always ends with ESC[0m.
[[nodiscard]] std::string build_map_row(const RenderInput& input, const RenderConfig& config,
                                        int y, int x0, int cols) {
    const bool ansi = config.use_ansi;
    const bool colored = config.use_color && config.use_ansi;
    const Map& map = *input.map;
    const VisibilityMap& visibility = *input.visibility;

    std::string row;
    row.reserve(static_cast<std::size_t>(cols) * (ansi ? 6 : 1));

    // Signature of the SGR style currently applied on this row. 0 means none;
    // remembered uses a sentinel distinct from every terrain colour code.
    constexpr int style_none = 0;
    constexpr int style_remembered = -1;
    bool style_active = false;
    int active_style = style_none;

    const auto reset_style = [&row, &style_active, &active_style]() {
        if (style_active) {
            row += "\033[0m";
            style_active = false;
            active_style = style_none;
        }
    };

    for (int offset = 0; offset < cols; ++offset) {
        const Coordinates here{x0 + offset, y};
        const bool is_actor = here == input.actor;

        if (!ansi) {
            if (is_actor) {
                row.push_back(actor_glyph);
            } else if (visibility.at(here) == CellVisibility::unexplored) {
                row.push_back(' ');
            } else {
                // Plain mode cannot distinguish remembered from visible without
                // changing canonical glyphs, so both use the terrain glyph.
                row.push_back(symbol_of(map.terrain_at(here)));
            }
            continue;
        }

        if (is_actor) {
            // Close any preceding remembered/visible run so its style cannot
            // bleed onto the actor cell.
            reset_style();
            if (colored) {
                row += "\033[1m";  // Bold.
                if (config.emphasis && input.emphasize_actor) {
                    row += "\033[7m";  // One-frame reverse-video emphasis.
                }
                row += sgr(color_actor);
                row.push_back(actor_glyph);
                row += "\033[0m";
                style_active = false;
                active_style = style_none;
            } else {
                // No colour: match the pre-fog bare-glyph actor. Emit no actor
                // bold/reverse/colour escape; the reset above already cleared
                // any active remembered dim.
                row.push_back(actor_glyph);
            }
            continue;
        }

        const CellVisibility state = visibility.at(here);
        if (state == CellVisibility::unexplored) {
            reset_style();
            row.push_back(' ');
            continue;
        }

        if (state == CellVisibility::remembered) {
            if (active_style != style_remembered) {
                reset_style();
                row += "\033[2m";  // Dim: exploration memory.
                style_active = true;
                active_style = style_remembered;
            }
            row.push_back(symbol_of(map.terrain_at(here)));
            continue;
        }

        // Currently visible terrain: normal styling, never dimmed.
        if (colored) {
            const int code = color_for(map.terrain_at(here));
            if (active_style != code) {
                reset_style();
                row += sgr(code);
                style_active = true;
                active_style = code;
            }
        } else {
            reset_style();
        }
        row.push_back(symbol_of(map.terrain_at(here)));
    }

    reset_style();
    return row;
}

// --- HUD text ---------------------------------------------------------------

[[nodiscard]] std::string position_text(Coordinates actor) {
    return "(" + std::to_string(actor.x) + "," + std::to_string(actor.y) + ")";
}

// The current/maximum stamina pair shared by every HUD layout.
[[nodiscard]] std::string stamina_text(const RenderInput& input) {
    return std::to_string(input.stamina) + "/" + std::to_string(input.max_stamina);
}

[[nodiscard]] std::string recent_text(const std::vector<RecentMove>& recent) {
    if (recent.empty()) {
        return "Recent: (none)";
    }
    std::string text = "Recent:";
    for (const RecentMove& move : recent) {
        char letter = direction_letter(move.direction);
        // Upper-case = the move landed; lower-case = it was blocked.
        if (move.result != MoveResult::moved) {
            letter = static_cast<char>(letter - 'A' + 'a');
        }
        text.push_back(' ');
        text.push_back(letter);
    }
    return text;
}

[[nodiscard]] std::string debug_text(const RenderInput& input, TerminalSize size) {
    const Map& map = *input.map;
    const std::size_t flat = static_cast<std::size_t>(input.actor.y) * map.width() +
                             static_cast<std::size_t>(input.actor.x);
    std::string text = "Debug: map ";
    text += std::to_string(map.width()) + "x" + std::to_string(map.height());
    text += "  actor" + position_text(input.actor) + " idx=" + std::to_string(flat);
    text += "  attempts=" + std::to_string(input.attempt_count);
    text += "  view=" + std::to_string(size.columns) + "x" + std::to_string(size.rows);
    return text;
}

// --- Frame assembly ---------------------------------------------------------

[[nodiscard]] Frame too_small_panel(int columns, int rows) {
    Frame frame(static_cast<std::size_t>(std::max(rows, 1)), std::string());
    const std::array<std::string, 2> lines{
        std::string("Window too small"),
        "Need " + std::to_string(standard_min_columns) + "x" +
            std::to_string(standard_min_map_rows + 4)};
    const int first = std::max(0, rows / 2 - 1);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::size_t target = static_cast<std::size_t>(first) + i;
        if (target < frame.size()) {
            frame[target] = center_plain(lines[i], columns);
        }
    }
    return frame;
}

// Assemble the final frame: an optional title, the vertically-centred map
// viewport, blank fillers, and the HUD pinned to the bottom. The total is
// clamped to exactly `rows` lines so nothing scrolls.
[[nodiscard]] Frame assemble(const RenderInput& input, const RenderConfig& config, int columns,
                             int rows, bool standard, TerminalSize size) {
    const Map& map = *input.map;

    std::vector<std::string> hud;
    if (standard) {
        hud.push_back("Pos " + position_text(input.actor) + "   Stamina: " + stamina_text(input) +
                      "   Terrain: " + terrain_name(input.terrain) +
                      "   Moves: " + std::to_string(input.move_count));
        hud.push_back("> " + input.message);
        hud.push_back(recent_text(input.recent));
        if (config.debug) {
            hud.push_back(debug_text(input, size));
        }
    } else {
        std::string status = position_text(input.actor) + " S:" + stamina_text(input) + " " +
                             terrain_name(input.terrain) + "  M:" + std::to_string(input.move_count) +
                             "  " + input.message;
        hud.push_back(std::move(status));
        if (config.debug) {
            hud.push_back(debug_text(input, size));
        }
    }
    for (std::string& line : hud) {
        line = fit_plain(std::move(line), columns);
    }

    const int title_rows = standard ? 1 : 0;
    const int hud_rows = static_cast<int>(hud.size());
    const int map_region = std::max(0, rows - title_rows - hud_rows);

    const int view_rows = std::min<int>(static_cast<int>(map.height()), map_region);
    const int view_cols = std::min({static_cast<int>(map.width()), columns});
    const int origin_x = viewport_origin(input.actor.x, static_cast<int>(map.width()), view_cols);
    const int origin_y = viewport_origin(input.actor.y, static_cast<int>(map.height()), view_rows);
    const int left_pad = std::max(0, (columns - view_cols) / 2);

    std::vector<std::string> map_lines;
    map_lines.reserve(static_cast<std::size_t>(std::max(0, view_rows)));
    for (int row = 0; row < view_rows; ++row) {
        std::string line(static_cast<std::size_t>(left_pad), ' ');
        line += build_map_row(input, config, origin_y + row, origin_x, view_cols);
        map_lines.push_back(std::move(line));
    }

    const int filler = std::max(0, map_region - view_rows);
    const int top_filler = filler / 2;
    const int bottom_filler = filler - top_filler;

    Frame frame;
    frame.reserve(static_cast<std::size_t>(rows));
    if (standard) {
        frame.push_back(fit_plain("NAM - arrows/WASD move, q to quit", columns));
    }
    frame.insert(frame.end(), static_cast<std::size_t>(top_filler), std::string());
    for (std::string& line : map_lines) {
        frame.push_back(std::move(line));
    }
    frame.insert(frame.end(), static_cast<std::size_t>(bottom_filler), std::string());
    for (std::string& line : hud) {
        frame.push_back(std::move(line));
    }

    // Defensive clamp so the frame is exactly `rows` lines regardless of rounding.
    if (frame.size() > static_cast<std::size_t>(rows)) {
        frame.resize(static_cast<std::size_t>(rows));
    } else {
        while (frame.size() < static_cast<std::size_t>(rows)) {
            frame.emplace_back();
        }
    }
    return frame;
}

}  // namespace

Frame Renderer::render(const RenderInput& input, TerminalSize size) const {
    int columns = size.valid() ? size.columns : fallback_columns;
    int rows = size.valid() ? size.rows : fallback_rows;

    if (columns < absolute_min_columns || rows < absolute_min_rows) {
        return too_small_panel(columns, rows);
    }

    const int hud_rows = config_.debug ? 4 : 3;
    const bool standard = columns >= standard_min_columns &&
                          rows >= (1 + hud_rows + standard_min_map_rows);

    const TerminalSize effective{columns, rows};
    return assemble(input, config_, columns, rows, standard, effective);
}

std::string Renderer::render_plain(const RenderInput& input) const {
    const Map& map = *input.map;

    std::string text;
    for (int y = 0; y < static_cast<int>(map.height()); ++y) {
        text += build_map_row(input, RenderConfig{false, false, config_.debug, false}, y, 0,
                              static_cast<int>(map.width()));
        text.push_back('\n');
    }
    text += "Pos " + position_text(input.actor) + "  Stamina: " + stamina_text(input) +
            "  Terrain: " + terrain_name(input.terrain) +
            "  Moves: " + std::to_string(input.move_count) + "\n";
    text += input.message + "\n";
    if (config_.debug) {
        text += recent_text(input.recent) + "\n";
    }
    return text;
}

}  // namespace nam::console
