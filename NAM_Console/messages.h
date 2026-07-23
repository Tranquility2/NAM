#pragma once

#include <string>

#include "direction.h"
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
// latest-event line. Successful and insufficient-stamina outcomes include the
// rule-provided stamina cost carried in the MoveOutcome; boundary and
// impassable-terrain outcomes have no cost and keep their existing wording.
[[nodiscard]] std::string describe_move(const MoveOutcome& outcome);

// A sentence describing a rest command for the HUD's latest-event line. A rest
// that recovered one or more stamina reports the exact amount; a rest at full
// stamina reports that stamina is already full.
[[nodiscard]] std::string describe_rest(const RestedEvent& rested);

// A user-facing explanation of why a map failed to load, including the source
// and line/column when the parser reported them.
[[nodiscard]] std::string describe_map_error(const MapLoadError& error);

// The HUD objective line for the current expedition phase. Seeking names the
// beacon and its glyph and asks the player to reach it and return; returning asks
// only to return to spawn; completed reports the finished expedition by name.
[[nodiscard]] std::string objective_line(const BeaconObjective& objective);

// A single bounded "Goal:" line summarising the objective phase for the compact
// layout, which has no room for the full objective sentence.
[[nodiscard]] std::string goal_line(const BeaconObjective& objective);

// The latest-event message shown when a move first enters the beacon cell,
// replacing the ordinary move wording for that command.
[[nodiscard]] std::string describe_beacon_discovered(const std::string& name);

// The latest-event message shown when a move completes the return to spawn,
// replacing the ordinary move wording for that command.
[[nodiscard]] std::string describe_expedition_completed(const std::string& name);

// The initial and final message for a single-reachable-cell map, where the
// beacon coincides with spawn and the expedition is already complete.
[[nodiscard]] std::string describe_spawn_beacon(const std::string& name);

}  // namespace nam::console
