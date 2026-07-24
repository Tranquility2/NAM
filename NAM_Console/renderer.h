#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "app_state.h"
#include "coordinates.h"
#include "frame.h"
#include "journal.h"
#include "map.h"
#include "objective.h"
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

    // Optional beacon objective to present. Production input always supplies the
    // core-owned objective; renderer-only fixtures may leave it null to render a
    // frame without any objective overlay or objective line.
    const BeaconObjective* objective = nullptr;
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

// The beacon objective glyph. A semantic overlay drawn on the beacon cell when it
// is visible or remembered and the actor is not standing on it; it never replaces
// or mutates the underlying Terrain.
inline constexpr char beacon_glyph = '*';

// A frontend-only summary of a completed expedition, shown on the completion
// screen. It carries the generated beacon name and the HUD/game counters as they
// stood immediately after the completing move: the successful move count, the
// total attempt count, and the final current/maximum stamina. It holds no core
// type and never exposes seed text, map paths, or objective coordinates.
struct CompletionSummary {
    std::string beacon_name;
    std::size_t move_count = 0;
    std::size_t attempt_count = 0;
    std::uint32_t stamina = 0;
    std::uint32_t max_stamina = 0;
};

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

    // Build the interactive beacon-discovery screen for the given size. The frame
    // has exactly `size.rows` rows, keeps every row within `size.columns`, centres
    // the fixed discovery lines when space permits, uses the 80x24 fallback for an
    // unknown size, and falls back to the shared window-too-small panel below the
    // absolute minimum. It carries no ANSI escape bytes.
    [[nodiscard]] Frame render_discovery(const std::string& beacon_name, TerminalSize size) const;

    // Render the beacon-discovery screen as an ANSI-free plain-text block: the
    // exact discovery lines, one per line, with a single trailing newline.
    [[nodiscard]] std::string render_discovery_plain(const std::string& beacon_name) const;

    // Build the interactive expedition-completion screen, bounded exactly like
    // render_discovery, from a frontend-only completion summary.
    [[nodiscard]] Frame render_completion(const CompletionSummary& summary,
                                          TerminalSize size) const;

    // Render the expedition-completion screen as an ANSI-free plain-text block: the
    // exact completion lines, one per line, with a single trailing newline.
    [[nodiscard]] std::string render_completion_plain(const CompletionSummary& summary) const;

    // The number of journal entry rows one interactive journal page shows for the
    // given terminal size (GUD-004). ConsoleApp uses this single source of truth
    // to compute the newest page and to scroll by page so the application and the
    // renderer can never disagree. An unknown size resolves to the 80x24 fallback.
    [[nodiscard]] int journal_page_capacity(TerminalSize size) const;

    // Build the bounded interactive journal frame. Entries are shown in
    // chronological order from top to bottom; `scroll_top` is the index of the
    // topmost visible entry and is clamped into range so rendering stays pure and
    // in-bounds (REQ-025 / REQ-030 / REQ-032). The frame has exactly `size.rows`
    // rows and keeps every row within `size.columns`, uses the 80x24 fallback for
    // an unknown size, and reuses the shared window-too-small panel below the
    // absolute minimum. It carries no ANSI escape bytes.
    [[nodiscard]] Frame render_journal(const Journal& journal, int scroll_top,
                                       TerminalSize size) const;

    // Render the complete journal as an ANSI-free plain-text block: an
    // `EXPEDITION JOURNAL` header, every entry numbered from 1, the empty-state
    // placeholder when there are no entries, and a single trailing newline
    // (REQ-029). It is not terminal-height bounded because redirected output is
    // not screen-limited.
    [[nodiscard]] std::string render_journal_plain(const Journal& journal) const;

private:
    RenderConfig config_;
};

}  // namespace nam::console
