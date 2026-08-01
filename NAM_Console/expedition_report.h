#pragma once

#include <optional>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "coordinates.h"
#include "expedition.h"
#include "expedition_score.h"
#include "game_event.h"
#include "journal.h"
#include "map.h"
#include "objective.h"
#include "settings.h"
#include "terrain.h"
#include "visibility.h"

namespace nam::console {

// The final expedition report: a frontend-owned, deterministic snapshot built once
// after the level objective completes. It keeps every input as a typed value - the terrain map, the fog snapshot, the
// route coordinates, the world identity, the structured journal, and the core
// score - so only formatter functions ever turn it into prose or report lines
// (REQ-153). It holds no terminal dimensions, ANSI, or scrolling state.

// How the played world was produced. Drives the world-identity lines and the exact
// replay/run-again command (REQ-154).
enum class WorldSource {
    built_in,      // The built-in default map: replay command is `nam_console`.
    file_map,      // A map loaded from a file path.
    text_seed,     // A Tiny World from hashed --seed text.
    numeric_seed,  // A Tiny World from an exact --seed-number value.
};

// The typed world identity needed to describe and reproduce the played world. Raw
// user-controlled bytes (seed text, map path) are stored verbatim and only ever
// emitted through byte escaping, so no control byte can reach report output.
struct WorldIdentity {
    WorldSource source = WorldSource::built_in;
    std::string seed_text;           // Raw original --seed text (text_seed only).
    std::uint64_t numeric_seed = 0;  // Decimal world seed (text_seed and numeric_seed).
    std::string map_path;            // Raw map path (file_map only).
};

// Derive the world identity from resolved settings. A map path selects file_map; a
// text seed selects text_seed and carries its numeric hash; a numeric seed selects
// numeric_seed; otherwise the built-in map. Pure with respect to its input.
[[nodiscard]] WorldIdentity world_identity_from(const Settings& settings);

// The ordered route the actor traveled: the spawn cell followed by every
// successful movement destination, in event order. A blocked movement never
// appends a coordinate (REQ-153).
class RouteHistory {
public:
    // Seed the route at the spawn cell. The spawn is always the first coordinate,
    // so a zero-move expedition still has a one-cell route.
    explicit RouteHistory(Coordinates spawn) { cells_.push_back(spawn); }

    // Fold one ordered core event. A successful movement appends its destination;
    // a blocked movement or any non-movement event appends nothing.
    void record_event(const GameEvent& event);

    [[nodiscard]] const std::vector<Coordinates>& cells() const noexcept { return cells_; }
    // The final cell the actor occupied: the last recorded coordinate.
    [[nodiscard]] Coordinates final_position() const noexcept { return cells_.back(); }
    // Whether a coordinate was ever occupied along the route.
    [[nodiscard]] bool traveled(Coordinates position) const noexcept;

private:
    std::vector<Coordinates> cells_;
};

// The carried expedition state at the moment a report is built. Defaults describe
// a standalone single-level run, which is what a handcrafted or built-in map is,
// so a caller that has no expedition to report can simply omit it.
struct ExpeditionCarryover {
    std::uint32_t levels_completed = 1;
    std::uint32_t total_levels = 1;
    bool expedition_completed = true;

    // The optional content of the level this report describes.
    std::uint32_t discoveries_found = 0;
    std::uint32_t discovery_total = 0;

    // Running totals across every finished level, including this one.
    std::uint64_t expedition_score = 0;
    std::uint32_t expedition_discoveries_found = 0;
    std::uint32_t expedition_discoveries_available = 0;

    // The bonus that was in force for this level, and the one it earned for the
    // next level.
    ExpeditionBonus applied_bonus = ExpeditionBonus::none;
    ExpeditionBonus earned_bonus = ExpeditionBonus::none;

    // The tier the next level uses. Absent once the expedition is complete.
    std::optional<LevelTier> next_tier;
};

// The complete report snapshot. Map and visibility come first so the aggregate can
// be brace-initialized with the two non-default-constructible members while every
// remaining field value-initializes.
struct ExpeditionReport {
    // Construct with the two non-default-constructible snapshots; every other field
    // default-initializes and is filled by build_expedition_report.
    ExpeditionReport(Map map_snapshot, VisibilityMap visibility_snapshot)
        : map(std::move(map_snapshot)), visibility(std::move(visibility_snapshot)) {}

    Map map;                    // Typed terrain snapshot for the route map.
    VisibilityMap visibility;   // Typed fog snapshot: unexplored/remembered/visible.
    std::string landmark_name;
    Coordinates spawn{};
    Coordinates exit_cell{};
    Coordinates final_position{};
    std::vector<Coordinates> route;
    WorldIdentity identity;
    ExpeditionScore score{};
    Journal journal;            // Structured journal snapshot for the journal section.
    std::uint64_t move_count = 0;
    std::uint64_t blocked_attempts = 0;
    std::uint64_t optimal_route_length = 0;
    std::uint64_t explored_reachable_cells = 0;
    std::uint64_t total_reachable_cells = 0;
    ExpeditionCarryover carryover;
};

// Build the final report. The move count is the HUD's successful-move tally,
// since routine travel no longer reaches the journal (REQ-143). Blocked attempts
// are attempt_count minus move_count via comparison-before-subtraction. Explored
// reachable terrain is computed from the map and visibility snapshot by the core,
// and the total comes from the objective. `map` and `visibility` are snapshotted
// for the route map.
[[nodiscard]] ExpeditionReport build_expedition_report(
    const LevelObjective& objective, const Map& map, const VisibilityMap& visibility,
    const Journal& journal, const RouteHistory& route, const WorldIdentity& identity,
    std::uint64_t move_count, std::uint64_t attempt_count,
    const ExpeditionCarryover& carryover = ExpeditionCarryover{});

// The single result/status line (REQ-133).
[[nodiscard]] std::string format_report_result(const ExpeditionReport& report);

// The compact deterministic story line, derived only from typed report fields
// (REQ-133). It names the landmark and the move count; every other number belongs
// to the statistics section so the report never states the same figure twice.
[[nodiscard]] std::string format_report_story(const ExpeditionReport& report);

// The transparent statistics for the level this report describes (REQ-143 /
// REQ-144): score with its route and discovery halves, discoveries found out of
// the level's total, the route summary (moves, the shortest legal route length,
// blocked attempts), and explored reachable terrain out of the total.
[[nodiscard]] std::vector<std::string> format_report_statistics(const ExpeditionReport& report);

// The running expedition totals carried across levels: score, discoveries, and
// the one carried bonus as it is spent and earned. Empty for a standalone level,
// whose own statistics already are the whole expedition.
[[nodiscard]] std::vector<std::string> format_report_expedition(const ExpeditionReport& report);

// The world-identity and replay/run-again lines (REQ-154). All user-controlled
// bytes are escaped through format_seed_for_display.
[[nodiscard]] std::vector<std::string> format_report_identity(const ExpeditionReport& report);

// The full-dimension route map rows (REQ-153). Overlay priority per cell is final
// position `F`, exit `X`, spawn `S`, any traveled cell `*`, then fog/terrain: an
// unexplored cell is `?` and a remembered or visible cell is its canonical
// symbol_of glyph. Plain ASCII and deterministic.
[[nodiscard]] std::vector<std::string> format_report_route_map(const ExpeditionReport& report);

// The route-map legend lines describing every overlay glyph (REQ-153).
[[nodiscard]] std::vector<std::string> format_report_legend();

// The complete ordered logical report lines (REQ-153): the `EXPEDITION REPORT`
// banner, the result and story, the level statistics, the expedition totals when
// the run spans more than one level, the world identity, the route map,
// the route legend, and the `EXPEDITION JOURNAL` section with every entry numbered
// from 1. These are the single source of report text; the renderer only slices
// them into a bounded viewport or joins them into a plain block.
[[nodiscard]] std::vector<std::string> format_report_lines(const ExpeditionReport& report);

}  // namespace nam::console
