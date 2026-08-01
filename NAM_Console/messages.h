#pragma once

#include <cstdint>
#include <string>

#include "expedition.h"
#include "direction.h"
#include "level_feature.h"
#include "game_event.h"
#include "map_parser.h"
#include "move_outcome.h"
#include "objective.h"
#include "terrain.h"

// User-facing wording lives entirely in the frontend. The core returns typed,
// non-localized results (MoveResult, Terrain, MapLoadError); these helpers turn
// them into the English strings the console shows. Keeping this here is what
// lets the core stay presentation-free and reusable by other frontends.
namespace nam::console {

// A short lower-case noun for a terrain type, e.g. "open", "mountain".
[[nodiscard]] std::string terrain_name(Terrain terrain);

// A single-letter tag for a direction, used in the compact recent-move HUD.
[[nodiscard]] char direction_letter(Direction direction) noexcept;

// A full human-readable name for a direction, e.g. "up".
[[nodiscard]] std::string direction_name(Direction direction);

// A sentence describing the outcome of a move attempt, suitable for the HUD's
// latest-event line. A successful move names the terrain entered and the sight
// range that terrain grants, because sight is the only thing terrain now governs;
// boundary and impassable-terrain outcomes keep their existing wording.
[[nodiscard]] std::string describe_move(const MoveOutcome& outcome);

// The latest-event message shown when a move first enters a discovery cell,
// replacing the ordinary move wording for that command. Discoveries are optional,
// so the count makes the remaining reward legible without a separate HUD field.
[[nodiscard]] std::string describe_discovery_found(std::uint32_t found, std::uint32_t total);

// The latest-event message shown when a move first fires a vantage point. It
// replaces the ordinary move wording for that command, because the wide reveal is
// the thing that happened.
[[nodiscard]] std::string describe_vantage_reached();

// A user-facing explanation of why a map failed to load, including the source
// and line/column when the parser reported them.
[[nodiscard]] std::string describe_map_error(const MapLoadError& error);

// The HUD objective line for the current level phase: reach the landmark, then
// follow the revealed broad direction to the exit.
[[nodiscard]] std::string objective_line(const LevelObjective& objective);

// A single bounded "Goal:" line summarising the objective phase for the compact
// layout, which has no room for the full objective sentence.
[[nodiscard]] std::string goal_line(const LevelObjective& objective);

// The latest-event message shown when a move first enters the landmark cell,
// replacing the ordinary move wording for that command.
[[nodiscard]] std::string describe_landmark_discovered(const std::string& name);

// The latest-event message shown when a move reaches the exit,
// replacing the ordinary move wording for that command.
[[nodiscard]] std::string describe_level_completed(const std::string& name);

// The initial and final message for a single-reachable-cell map, where the
// exit coincides with spawn and the level is already complete.
[[nodiscard]] std::string describe_spawn_landmark(const std::string& name);

// The plain-mode reminder shown when the landmark-discovery screen is active and
// the player enters a command that neither dismisses it nor moves.
[[nodiscard]] std::string discovery_reminder();

// The plain-mode reminder shown when the completion screen is active and the
// player enters any command other than an acknowledgement.
[[nodiscard]] std::string completion_reminder();

// The HUD line that opens a level of a multi-level expedition, naming the tier,
// the position in the chain, and any bonus carried into it.
[[nodiscard]] std::string describe_level_started(LevelTier tier, std::uint32_t level_number,
                                                 std::uint32_t total_levels,
                                                 ExpeditionBonus bonus);

// The single line printed once on the restored normal screen after an
// interactive run whose level completed, naming the landmark it passed. While
// completion is being acknowledged this replaces every goodbye/EOF/interrupt
// line so the acknowledgement can never overwrite it.
[[nodiscard]] std::string restored_completion_message(const std::string& name);

}  // namespace nam::console
