#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "app_state.h"
#include "coordinates.h"
#include "expedition_report.h"
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
    std::uint32_t provisions = 0;           // Current provisions remaining.
    std::uint32_t starting_provisions = 0;  // Provisions the expedition began with.
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

// A frontend-only viewport into the final expedition report: the index of the
// topmost visible body line and the leftmost visible column. Both offsets are
// clamped by the renderer so scrolling can never leave the report content
// (GUD-004). The vertical offset counts logical report body lines (the sticky
// `EXPEDITION REPORT` title is not scrolled); the horizontal offset counts bytes,
// which for the ASCII report equal visible columns.
struct ReportViewport {
    int vertical = 0;
    int horizontal = 0;
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

    // Build the interactive rescue-acknowledgement screen for the given size. Like
    // the discovery screen it is a centred, ANSI-free panel of fixed humorous
    // lines announcing that provisions ran out and the explorer signaled for an
    // embarrassingly early pickup, shown before the final report (REQ-131).
    [[nodiscard]] Frame render_rescue(const std::string& beacon_name, TerminalSize size) const;

    // Render the rescue-acknowledgement screen as an ANSI-free plain-text block:
    // the exact rescue lines, one per line, with a single trailing newline. Used
    // by plain mode before the failed-expedition report (REQ-132).
    [[nodiscard]] std::string render_rescue_plain(const std::string& beacon_name) const;

    // Build the interactive expedition-completion report for the given size. The
    // frame has exactly `size.rows` rows and keeps every row within `size.columns`.
    // A fixed title row (the `EXPEDITION REPORT` banner) and a fixed control row
    // bracket a scrollable body; the body is sliced vertically by
    // `viewport.vertical` logical lines and horizontally by `viewport.horizontal`
    // bytes so every byte of a wide route-map row is reachable. Offsets are clamped
    // to range before slicing, an unknown size uses the 80x24 fallback, and a size
    // below the absolute minimum reuses the shared window-too-small panel. The
    // frame carries no ANSI escape bytes.
    [[nodiscard]] Frame render_report(const ExpeditionReport& report, ReportViewport viewport,
                                      TerminalSize size) const;

    // Render the complete expedition report as an ANSI-free plain-text block: every
    // logical report line in order, each terminated by a single LF, with exactly
    // one trailing LF and no terminal-dependent content (REQ-010).
    [[nodiscard]] std::string render_report_plain(const ExpeditionReport& report) const;

    // Clamp a requested report viewport into the valid range for the given size:
    // the vertical offset into [0, body lines minus one page] and the horizontal
    // offset into [0, longest body line minus visible columns]. This is the single
    // source of truth both ConsoleApp and rendering use so application updates and
    // frame composition can never disagree (GUD-004 / RISK-002). An unknown size
    // resolves to the 80x24 fallback.
    [[nodiscard]] ReportViewport clamp_report_viewport(const ExpeditionReport& report,
                                                       ReportViewport requested,
                                                       TerminalSize size) const;

    // The number of report body lines one interactive report page shows for the
    // given size, at least one. ConsoleApp uses this to scroll by a full page. An
    // unknown size resolves to the 80x24 fallback.
    [[nodiscard]] int report_page_capacity(TerminalSize size) const;

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
