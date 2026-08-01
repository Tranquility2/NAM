#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "coordinates.h"
#include "exploration.h"
#include "expedition_score.h"
#include "game_state.h"
#include "map.h"
#include "map_parser.h"
#include "objective.h"
#include "terrain.h"
#include "visibility.h"

namespace {

Map make_map(std::string_view text) {
    MapLoadResult result = load_map(text);
    REQUIRE(std::holds_alternative<Map>(result));
    return std::get<Map>(std::move(result));
}

}  // namespace

TEST_SUITE("game") {

// --- Objective route baseline -----------------------------------------------

TEST_CASE("the objective stores the exact shortest spawn-landmark-exit move count") {
    // A five-cell open corridor places the landmark at (2,0) and the exit at
    // (3,0), so the shortest legal route is three steps.
    const Map map = make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n");
    const LevelObjective objective = create_level_objective(map);
    CHECK(objective.landmark == Coordinates{2, 0});
    CHECK(objective.exit_cell == Coordinates{3, 0});
    CHECK(objective.minimum_route_length == 3);
}

TEST_CASE("the route length counts moves and never the terrain crossed") {
    // The same corridor paved with mountain instead of open ground. Terrain now
    // governs sight alone, so it must not change the length of the route.
    const Map map = make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.@@..\n");
    const LevelObjective objective = create_level_objective(map);
    CHECK(objective.landmark == Coordinates{2, 0});
    CHECK(objective.exit_cell == Coordinates{3, 0});
    // spawn -> (1,0) -> (2,0) -> (3,0): three steps, exactly as over open ground.
    CHECK(objective.minimum_route_length == 3);
}

TEST_CASE("the route length counts a detour a barrier forces") {
    // A wall between the landmark and the exit forces the second leg around it,
    // so the route is longer than the straight-line distance between the two.
    const Map map = make_map(
        "NAM-MAP 1\nwidth 5\nheight 3\nspawn 0 1\n---\n.....\n..#..\n.....\n");
    const LevelObjective objective = create_level_objective(map);
    const std::uint64_t straight_line =
        static_cast<std::uint64_t>(std::abs(objective.landmark.x - map.spawn().x) +
                                   std::abs(objective.landmark.y - map.spawn().y) +
                                   std::abs(objective.exit_cell.x - objective.landmark.x) +
                                   std::abs(objective.exit_cell.y - objective.landmark.y));
    CHECK(objective.minimum_route_length >= straight_line);
}

TEST_CASE("a single reachable spawn yields a zero-length route") {
    const Map map = make_map("NAM-MAP 1\nwidth 3\nheight 3\nspawn 1 1\n---\n###\n#.#\n###\n");
    const LevelObjective objective = create_level_objective(map);
    CHECK(objective.minimum_route_length == 0);
}

// --- Reachability ------------------------------------------------------------

TEST_CASE("exploration counts reflect reachable walkable cells and explored memory") {
    const Map map = make_map("NAM-MAP 1\nwidth 5\nheight 3\nspawn 0 1\n---\n#####\n.....\n#####\n");
    CHECK(count_reachable_walkable_cells(map) == 5);
    VisibilityMap visibility(map.width(), map.height());
    CHECK(count_explored_reachable_walkable_cells(map, visibility) == 0);
    // Reveal a 5x5 square around spawn (0,1): only columns 0..2 of the open row
    // fall within it, so three reachable walkable cells are explored so far.
    visibility.reveal_square(map.spawn(), 2);
    CHECK(count_explored_reachable_walkable_cells(map, visibility) == 3);
    // Revealing again from the far end covers the remaining columns.
    visibility.reveal_square(Coordinates{4, 1}, 2);
    CHECK(count_explored_reachable_walkable_cells(map, visibility) == 5);
}

// --- Level scoring -----------------------------------------------------------

TEST_CASE("an optimal run with no blocked moves scores the maximum") {
    CompletedScoreInput input;
    input.optimal_route_length = 12;
    input.actual_moves = 12;
    input.blocked_attempts = 0;

    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.value == completed_score_maximum);
    CHECK(score.value == 1000);
    CHECK(score.excess_moves == 0);
    CHECK(score.optimal_route_length == 12);
    CHECK(score.actual_moves == 12);
}

TEST_CASE("spending less than the optimal route never scores above the maximum") {
    CompletedScoreInput input;
    input.optimal_route_length = 12;
    input.actual_moves = 5;

    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.excess_moves == 0);
    CHECK(score.value == completed_score_maximum);
}

TEST_CASE("each excess stamina point subtracts five") {
    CompletedScoreInput input;
    input.optimal_route_length = 10;
    input.actual_moves = 13;

    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.excess_moves == 3);
    CHECK(score.value == completed_score_base - 3 * completed_penalty_per_excess_move);
    CHECK(score.value == 985);
}

TEST_CASE("each blocked move subtracts twenty") {
    CompletedScoreInput input;
    input.optimal_route_length = 10;
    input.actual_moves = 10;
    input.blocked_attempts = 4;

    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.blocked_attempts == 4);
    CHECK(score.value == completed_score_base - 4 * completed_penalty_per_blocked_attempt);
    CHECK(score.value == 920);
}

TEST_CASE("penalties accumulate and clamp at zero") {
    CompletedScoreInput input;
    input.optimal_route_length = 0;
    input.actual_moves = 100;   // 500 points of excess stamina.
    input.blocked_attempts = 100;       // 2000 points of blocked moves.

    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.value == 0);
}

TEST_CASE("scoring is overflow-safe at extreme counters") {
    constexpr std::uint64_t huge = std::numeric_limits<std::uint64_t>::max();
    CompletedScoreInput input;
    input.optimal_route_length = 0;
    input.actual_moves = huge;
    input.blocked_attempts = huge;

    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.value == 0);
    CHECK(score.excess_moves == huge);
}

}  // TEST_SUITE("game")
