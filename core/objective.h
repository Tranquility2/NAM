#pragma once

#include <cstdint>
#include <string>

#include "coordinates.h"
#include "direction.h"
#include "map.h"

// Frontend-neutral V2 level objective. Every map receives one deterministic exit
// placed on a distant scenic reachable cell. A named landmark is placed on a
// deterministic shortest path from spawn to that exit. The player reaches the
// landmark to reveal the broad exit direction, then completes the level by
// entering the exit; returning to spawn is never required.
//
// The exit is chosen from the
// cells whose shortest cardinal path from spawn is at least the minimum eligible
// distance (the exact integer ceiling of 75% of the greatest reachable distance),
// preferring hills and mountains and falling back to any distant walkable cell,
// then selected deterministically by a portable hash so the same map always
// yields the same exit. Placement, naming, bearing, and the status machine live
// entirely in the core so frontends only present state and typed transitions.

// The phase of one level objective. Compatibility aliases retain the old enum
// spellings while console and report code migrate in later V2 steps.
enum class ObjectiveStatus {
    seeking_landmark,
    seeking_exit,
    completed,
    seeking_beacon = seeking_landmark,
    returning_to_spawn = seeking_exit,
};

// The typed change a single committed actor position causes to the objective.
// Compatibility aliases retain the old event spellings while consumers migrate.
enum class ObjectiveTransition {
    none,
    landmark_discovered,
    level_completed,
    beacon_discovered = landmark_discovered,
    expedition_completed = level_completed,
};

// The complete objective owned by a level. `beacon` temporarily retains the old
// field name for the distant exit while report/scoring code is simplified; it is
// semantic overlay state and never a terrain value.
struct LevelObjective {
    Coordinates landmark{};
    Coordinates beacon{};
    std::string name;
    Direction exit_bearing = Direction::right;
    ObjectiveStatus status = ObjectiveStatus::seeking_landmark;
    // The deterministic cheapest round-trip stamina cost for this expedition: the
    // minimum terrain-entry stamina cost of a walkable cardinal path from spawn to
    // the beacon plus the minimum terrain-entry stamina cost of a walkable
    // cardinal path from the beacon back to spawn. Edge weights are
    // stamina_cost_of(destination), the single terrain-cost/walkability source, so
    // this scalar is a pure objective property every frontend can reuse to score a
    // run. It is 0 when the beacon coincides with spawn (a single-cell map).
    std::uint64_t minimum_round_trip_stamina_cost = 0;
    // The deterministic minimum number of provisions that make this expedition
    // feasible: the lexicographic (overnight transitions, provisions) planning
    // search in expedition_planning selects the plan with the fewest overnight
    // camps/bivouacs and, among those, the fewest provisions consumed. This field
    // is that provisions component. It is 0 when the beacon coincides with spawn.
    // Frontends start a run with this baseline plus one spare provision.
    std::uint64_t minimum_required_provisions = 0;
    // The deterministic minimum number of numbered days the expedition needs to
    // complete the beacon round trip: one more than the fewest overnight
    // transitions in the planning search (REQ-016). It is 1 when the beacon
    // coincides with spawn (initial single-cell completion).
    std::uint32_t minimum_completion_days = 1;
    // The deterministic deadline day, two days beyond minimum_completion_days
    // (REQ-019). An action that leaves the objective incomplete at hour 12 on this
    // day, or with no continuation on this day, ends the expedition as overdue.
    std::uint32_t deadline_days = 3;
    // The number of walkable cells reachable from spawn over cardinal walkable
    // steps, including spawn itself. A pure map/objective property used as the
    // denominator of the rescued exploration score, so every frontend agrees on
    // the exploration fraction.
    std::uint64_t total_reachable_walkable_cells = 0;
};

using BeaconObjective = LevelObjective;

// The before/after status and typed transition around one movement command,
// nested into the movement event so consumers observe objective progress in the
// same ordered stream as movement, without a second event per command.
struct ObjectiveUpdate {
    ObjectiveStatus before = ObjectiveStatus::seeking_landmark;
    ObjectiveStatus after = ObjectiveStatus::seeking_landmark;
    ObjectiveTransition transition = ObjectiveTransition::none;
};

// Build the complete initial objective for a map: run a cardinal breadth-first
// search from the map's spawn over walkable terrain, find the greatest reachable
// shortest-path distance, and collect the distant candidate cells (at least the
// minimum eligible distance, which is the exact integer ceiling of 75% of that
// greatest distance). Scenic candidates (hills and mountains, treated as one
// pool) are preferred; when none are distant enough the full distant walkable
// pool is used. The exit is the candidate chosen by hashing the map-and-spawn
// fingerprint modulo the row-major candidate count, so selection is deterministic
// and platform-independent rather than always the single farthest cell. The
// landmark is the middle non-spawn cell of a deterministic shortest path to the
// exit, with adjacent exits revealed from spawn. When spawn is the only reachable
// walkable cell the objective starts completed.
//
// `max_stamina` is the stamina cap used by the deterministic minimum-provision
// search; callers pass GameState::maximum_stamina so the objective and the game
// share one cap. The default matches the current baseline for renderer-only and
// objective-only fixtures that do not construct a GameState.
[[nodiscard]] LevelObjective create_beacon_objective(const Map& map,
                                                     std::uint32_t max_stamina = 20);

// Advance the objective for a committed actor position and return the exact
// transition it caused. Entering the landmark while seeking reveals the exit;
// entering the exit after that completes the level.
ObjectiveTransition advance_objective(LevelObjective& objective, Coordinates actor);

// The semantic overlay target for the current phase: landmark before discovery,
// exit afterward. A completed objective keeps the exit as its final target.
[[nodiscard]] constexpr Coordinates objective_target(const LevelObjective& objective) noexcept {
    return objective.status == ObjectiveStatus::seeking_landmark ? objective.landmark
                                                                 : objective.beacon;
}
