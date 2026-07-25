#pragma once

#include <cstdint>
#include <string>

#include "coordinates.h"
#include "map.h"

// Frontend-neutral beacon objective: every map receives one deterministic named
// beacon placed on a distant scenic reachable cell. The beacon is chosen from the
// cells whose shortest cardinal path from spawn is at least the minimum eligible
// distance (the exact integer ceiling of 75% of the greatest reachable distance),
// preferring hills and mountains and falling back to any distant walkable cell,
// then selected deterministically by a portable hash so the same map always
// yields the same beacon. The player must enter the beacon cell and then return
// to spawn to complete the expedition. Placement, naming, and the status machine
// live entirely in the core so any frontend only presents this state and reacts
// to the typed transitions it produces.

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
    // The deterministic cheapest round-trip stamina cost for this expedition: the
    // minimum terrain-entry stamina cost of a walkable cardinal path from spawn to
    // the beacon plus the minimum terrain-entry stamina cost of a walkable
    // cardinal path from the beacon back to spawn. Edge weights are
    // stamina_cost_of(destination), the single terrain-cost/walkability source, so
    // this scalar is a pure objective property every frontend can reuse to score a
    // run. It is 0 when the beacon coincides with spawn (a single-cell map).
    std::uint64_t minimum_round_trip_stamina_cost = 0;
    // The deterministic minimum number of provisions that make this expedition
    // feasible: a finite state search over (position, stamina 0..maximum, phase)
    // that minimizes provisions consumed to travel from spawn to the beacon and
    // back, where a move needs stamina_cost_of(destination) stamina and a rest
    // spends one provision to recover rest_recovery_of(current terrain) capped at
    // the stamina maximum. It is 0 when the beacon coincides with spawn. Frontends
    // start a run with this baseline plus one spare provision.
    std::uint64_t minimum_required_provisions = 0;
    // The number of walkable cells reachable from spawn over cardinal walkable
    // steps, including spawn itself. A pure map/objective property used as the
    // denominator of the rescued exploration score, so every frontend agrees on
    // the exploration fraction.
    std::uint64_t total_reachable_walkable_cells = 0;
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
// search from the map's spawn over walkable terrain, find the greatest reachable
// shortest-path distance, and collect the distant candidate cells (at least the
// minimum eligible distance, which is the exact integer ceiling of 75% of that
// greatest distance). Scenic candidates (hills and mountains, treated as one
// pool) are preferred; when none are distant enough the full distant walkable
// pool is used. The beacon is the candidate chosen by hashing the map-and-spawn
// fingerprint modulo the row-major candidate count, so selection is deterministic
// and platform-independent rather than always the single farthest cell. The
// deterministic name is then generated and the initial status is set. When spawn
// is the only reachable walkable cell the beacon is placed at spawn and the
// objective starts completed.
//
// `max_stamina` is the stamina cap used by the deterministic minimum-provision
// search; callers pass GameState::maximum_stamina so the objective and the game
// share one cap. The default matches the current baseline for renderer-only and
// objective-only fixtures that do not construct a GameState.
[[nodiscard]] BeaconObjective create_beacon_objective(const Map& map, std::uint32_t max_stamina = 20);

// Advance the objective for a committed actor position and return the exact
// transition it caused. Only a successful move that first enters the beacon cell
// (while seeking) yields beacon_discovered; only a successful move onto spawn
// after discovery yields expedition_completed. Every other position leaves the
// status unchanged and returns none.
ObjectiveTransition advance_objective(BeaconObjective& objective, Coordinates actor,
                                      Coordinates spawn);
