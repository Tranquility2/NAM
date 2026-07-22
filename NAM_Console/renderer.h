#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "app_state.h"
#include "coordinates.h"
#include "frame.h"
#include "map.h"
#include "terminal.h"
#include "terrain.h"
#include "visibility.h"

namespace nam::console {

// Everything the renderer needs about the world for one frame. It is plain data
// (the map and visibility are referenced, not owned) so a frame can be produced
// and inspected without a terminal, which keeps rendering testable.
//
// Both `map` and `visibility` are required for rendering: the renderer branches
// on per-cell CellVisibility before reading terrain, so a null visibility
// pointer is a caller error.
struct RenderInput {
    const Map* map = nullptr;
    const VisibilityMap* visibility = nullptr;
    Coordinates actor{};
    Terrain terrain{};
    std::size_t move_count = 0;
    std::size_t attempt_count = 0;
    std::uint32_t stamina = 0;
    std::uint32_t max_stamina = 0;
    std::string message;
    std::vector<RecentMove> recent;
    bool emphasize_actor = false;  // One-frame emphasis after a successful move.
};

// How the renderer is allowed to draw. `use_ansi` gates all control/colour
// escapes: when false (a non-VT Windows console) the renderer emits plain rows
// and the backend positions the cursor natively.
struct RenderConfig {
    bool use_color = true;
    bool use_ansi = true;
    bool debug = false;
    bool emphasis = true;  // Master switch for move emphasis (--no-animation).
};

// The player glyph. Chosen so it collides with no terrain symbol
// ('.', '@', '~', 'x', '^', '=', '|').
inline constexpr char actor_glyph = 'O';

// Composes frames from world state. Pure with respect to its inputs: the same
// RenderInput and size always yield the same Frame, and nothing is written to
// any stream here.
class Renderer {
public:
    explicit Renderer(RenderConfig config) noexcept : config_(config) {}

    [[nodiscard]] const RenderConfig& config() const noexcept { return config_; }

    // Build the interactive frame for the given terminal size. Handles adaptive
    // compact/standard layouts, viewport scrolling on large maps, and a centred
    // "window too small" panel. The result always has at most `size.rows` rows,
    // each at most `size.columns` visible columns, so it can never scroll the
    // terminal or corrupt absolute positioning.
    [[nodiscard]] Frame render(const RenderInput& input, TerminalSize size) const;

    // Render a self-contained plain-text block (no cursor moves, no colour) for
    // line-oriented mode: readable in redirected output and by screen readers.
    [[nodiscard]] std::string render_plain(const RenderInput& input) const;

private:
    RenderConfig config_;
};

}  // namespace nam::console
