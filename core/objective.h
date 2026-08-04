#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "coordinates.h"
#include "direction.h"
#include "landmark.h"
#include "map.h"

// Frontend-neutral V2 level objective. Every map receives one deterministic exit.
// A generated map carries the exit its level template authored, so the objective
// simply honors it; a handcrafted map has none, so the exit is derived and placed
// on a distant scenic reachable cell. Either way a named landmark is placed on a
// deterministic shortest path from spawn to that exit. The player reaches the
// landmark to reveal the broad exit direction, then completes the level by
// entering the exit; returning to spawn is never required.
//
// A derived exit is chosen from the
// cells whose shortest cardinal path from spawn is at least the minimum eligible
// distance (the exact integer ceiling of 75% of the greatest reachable distance),
// preferring hills and mountains and falling back to any distant walkable cell,
// then selected deterministically by a portable hash so the same map always
// yields the same exit. Placement, naming, bearing, and the status machine live
// entirely in the core so frontends only present state and typed transitions.

// The phase of one level objective.
enum class ObjectiveStatus {
    seeking_landmark,
    seeking_exit,
    completed,
};

// The typed change a single committed actor position causes to the objective.
enum class ObjectiveTransition {
    none,
    landmark_discovered,
    level_completed,
};

// The complete objective owned by a level. `landmark` and `exit_cell` are
// semantic overlay state and never terrain values.
struct LevelObjective {
    Coordinates landmark{};
    Coordinates exit_cell{};
    // What the landmark is, and therefore what terrain it stands on. The kind is
    // always `landmark_kind_of(terrain at the landmark)`, so the map and the name
    // can never disagree about the same place.
    LandmarkKind landmark_kind = LandmarkKind::waystone;
    std::string name;
    // The broad, truthful direction from the landmark to the exit, revealed when
    // the landmark is discovered. It is deliberately coarse: it names the dominant
    // cardinal component only, so it halves the search rather than solving it.
    Direction exit_bearing = Direction::right;
    ObjectiveStatus status = ObjectiveStatus::seeking_landmark;
    // The deterministic length in moves of the shortest legal level route: the
    // fewest walkable cardinal steps from spawn to the landmark plus the fewest
    // from the landmark to the exit. It is a pure objective property every
    // frontend can reuse to score a run, and it is 0 when the exit coincides with
    // spawn (a single-cell map).
    std::uint64_t minimum_route_length = 0;
    // The number of walkable cells reachable from spawn over cardinal walkable
    // steps, including spawn itself. A pure map/objective property used as the
    // denominator of the exploration statistic, so every frontend agrees on
    // the exploration fraction.
    std::uint64_t total_reachable_walkable_cells = 0;
};

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
[[nodiscard]] LevelObjective create_level_objective(const Map& map);

// The deterministic cheapest cardinal path from `source` to `target` over walkable
// cells, inclusive of both ends. Empty when the target is unreachable, and a
// single cell when source and target coincide. Neighbours are visited in the fixed
// Direction order, so the chosen path among equal-length ones is stable on every
// platform. Exposed because objectives, frontends, and tests all need to talk
// about the same route.
[[nodiscard]] std::vector<Coordinates> shortest_path(const Map& map, Coordinates source,
                                                     Coordinates target);

// Advance the objective for a committed actor position and return the exact
// transition it caused. Entering the landmark while seeking reveals the exit;
// entering the exit after that completes the level.
ObjectiveTransition advance_objective(LevelObjective& objective, Coordinates actor);

// The semantic overlay target for the current phase: landmark before discovery,
// exit afterward. A completed objective keeps the exit as its final target.
[[nodiscard]] constexpr Coordinates objective_target(const LevelObjective& objective) noexcept {
    return objective.status == ObjectiveStatus::seeking_landmark ? objective.landmark
                                                                 : objective.exit_cell;
}
