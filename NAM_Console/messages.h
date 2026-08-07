#pragma once

#include <cstdint>
#include <string>

#include "expedition.h"
#include "direction.h"
#include "level_feature.h"
#include "game_event.h"
#include "map_parser.h"
#include "move_outcome.h"
#include "set_piece.h"
#include "vantage.h"
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
[[nodiscard]] std::string describe_vantage_reached(VantageKind kind);

// The latest-event message shown on the move that first steps into the level's
// terrain set-piece. Every route crosses it, so this is the one moment of the
// level the player is certain to be told about.
[[nodiscard]] std::string describe_set_piece_crossed(SetPieceKind kind);

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

// The player-facing name of a bonus, capitalized for a label or the start of a
// sentence, e.g. "Keen eye". Empty for ExpeditionBonus::none.
//
// This and `bonus_effect_clause` are the only places a bonus is put into words.
// Every site that names one - the HUD, the level-start line, and the report -
// goes through them, because the report used to hardcode "keen eye" and so named
// the wrong bonus for every surveyor and pathfinder run.
[[nodiscard]] std::string bonus_name(ExpeditionBonus bonus);

// The same name in the lower case a mid-sentence mention needs, e.g. "keen eye".
[[nodiscard]] std::string bonus_name_lower(ExpeditionBonus bonus);

// What a bonus does to the level it applies to, as a clause completing a phrase
// such as "this level's" or "the next level's", e.g. "discoveries are worth
// double". Empty for ExpeditionBonus::none.
[[nodiscard]] std::string bonus_effect_clause(ExpeditionBonus bonus);

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
