#pragma once

#include <string>

#include "coordinates.h"
#include "map.h"

// Frontend-neutral beacon objective: every map receives one deterministic named
// beacon placed on the reachable walkable cell whose shortest cardinal path from
// spawn contains the greatest number of moves. The player must enter the beacon
// cell and then return to spawn to complete the expedition. Placement, naming,
// and the status machine live entirely in the core so any frontend only presents
// this state and reacts to the typed transitions it produces.

// The phase of the beacon expedition. A new nontrivial objective starts at
// seeking_beacon; entering the beacon cell moves it to returning_to_spawn; a
// later return onto spawn moves it to completed. A single-reachable-cell map
// starts already completed at spawn.
enum class ObjectiveStatus {
    seeking_beacon,
    returning_to_spawn,
    completed,
};

// The typed change a single committed actor position causes to the objective.
// `none` means the status did not change; `beacon_discovered` marks the move
// that first entered the beacon cell; `expedition_completed` marks the move that
// returned to spawn after discovery.
enum class ObjectiveTransition {
    none,
    beacon_discovered,
    expedition_completed,
};

// The complete objective owned by a game: where the beacon sits, its generated
// name, and the current expedition status. The beacon is semantic overlay state,
// never a terrain value, so map serialization and movement cost stay unchanged.
struct BeaconObjective {
    Coordinates beacon{};
    std::string name;
    ObjectiveStatus status = ObjectiveStatus::seeking_beacon;
};

// The before/after status and typed transition around one movement command,
// nested into the movement event so consumers observe objective progress in the
// same ordered stream as movement, without a second event per command.
struct ObjectiveUpdate {
    ObjectiveStatus before = ObjectiveStatus::seeking_beacon;
    ObjectiveStatus after = ObjectiveStatus::seeking_beacon;
    ObjectiveTransition transition = ObjectiveTransition::none;
};

// Build the complete initial objective for a map: run a cardinal breadth-first
// search from the map's spawn over walkable terrain, select the reachable
// walkable cell with the greatest shortest-path move count (earliest row-major on
// ties), generate the deterministic name, and set the initial status. When spawn
// is the only reachable walkable cell the beacon is placed at spawn and the
// objective starts completed.
[[nodiscard]] BeaconObjective create_beacon_objective(const Map& map);

// Advance the objective for a committed actor position and return the exact
// transition it caused. Only a successful move that first enters the beacon cell
// (while seeking) yields beacon_discovered; only a successful move onto spawn
// after discovery yields expedition_completed. Every other position leaves the
// status unchanged and returns none.
ObjectiveTransition advance_objective(BeaconObjective& objective, Coordinates actor,
                                      Coordinates spawn);
