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

TEST_SUITE("expedition_score") {

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

TEST_CASE("a fully explored level inside the budget scores every component") {
    CompletedScoreInput input;
    input.explored_reachable_cells = 160;
    input.total_reachable_cells = 160;
    input.actual_moves = 40;
    input.discoveries_found = 1;

    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.completion_value == completed_exit_award);
    CHECK(score.exploration_value == completed_exploration_maximum);
    CHECK(score.explored_percent == 100);
    CHECK(score.discovery_value == completed_score_per_discovery);
    CHECK(score.budget_value == completed_budget_award);
    CHECK(score.value == 1000 + 800 + 200 + 200);
}

TEST_CASE("reaching the exit is worth more than a perfect sweep") {
    // The priority order has to hold in the arithmetic: finishing levels is
    // always the largest single contribution a level can make.
    CHECK(completed_exit_award > completed_exploration_maximum);
    CHECK(completed_exploration_maximum > completed_budget_award);

    CompletedScoreInput blind;
    blind.total_reachable_cells = 100;
    blind.actual_moves = 10;
    CHECK(compute_completed_score(blind).value == completed_exit_award + completed_budget_award);
}

TEST_CASE("the exploration award scales linearly with the explored fraction") {
    CompletedScoreInput input;
    input.total_reachable_cells = 200;
    input.actual_moves = 10;

    input.explored_reachable_cells = 50;
    CHECK(compute_completed_score(input).exploration_value == completed_exploration_maximum / 4);
    input.explored_reachable_cells = 100;
    CHECK(compute_completed_score(input).exploration_value == completed_exploration_maximum / 2);
    input.explored_reachable_cells = 150;
    CHECK(compute_completed_score(input).exploration_value ==
          completed_exploration_maximum * 3 / 4);
}

TEST_CASE("uncovering more of a level never scores less than uncovering less") {
    // The design's central invariant. Exploring costs moves, so the only bucket
    // that can push back is the budget one - and it is bounded well below what a
    // meaningful gain in coverage is worth.
    CompletedScoreInput rushed;
    rushed.total_reachable_cells = 163;
    rushed.explored_reachable_cells = 60;
    rushed.actual_moves = 10;

    CompletedScoreInput thorough = rushed;
    thorough.explored_reachable_cells = 163;
    // A wandering route far past the budget, so the budget bucket is empty.
    thorough.actual_moves = 163 * 4;

    const ExpeditionScore rushed_score = compute_completed_score(rushed);
    const ExpeditionScore thorough_score = compute_completed_score(thorough);
    CHECK(rushed_score.budget_value == completed_budget_award);
    CHECK(thorough_score.budget_value == 0);
    CHECK(thorough_score.value > rushed_score.value);
}

TEST_CASE("the explored percentage floors rather than rounding up") {
    CompletedScoreInput input;
    input.total_reachable_cells = 3;
    input.explored_reachable_cells = 2;
    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.explored_percent == 66);
    CHECK(score.exploration_value == 533);
}

TEST_CASE("an explored count above the total cannot inflate the fraction") {
    CompletedScoreInput input;
    input.total_reachable_cells = 10;
    input.explored_reachable_cells = 999;
    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.explored_reachable_cells == 10);
    CHECK(score.explored_percent == 100);
    CHECK(score.exploration_value == completed_exploration_maximum);
}

TEST_CASE("a level with no reachable cells scores no exploration and no budget") {
    CompletedScoreInput input;
    input.actual_moves = 4;
    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.explored_percent == 0);
    CHECK(score.exploration_value == 0);
    CHECK(score.move_budget == 0);
    CHECK(score.moves_over_budget == 4);
    CHECK(score.budget_value == completed_budget_award - 4 * completed_penalty_per_move_over_budget);
}

// --- The soft move budget ----------------------------------------------------

TEST_CASE("the move budget is one move per reachable walkable cell") {
    CHECK(move_budget_for(163) == 163);

    CompletedScoreInput input;
    input.total_reachable_cells = 163;
    input.actual_moves = 163;
    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.move_budget == 163);
    CHECK(score.moves_over_budget == 0);
    CHECK(score.budget_value == completed_budget_award);
}

TEST_CASE("every move past the budget costs five and the loss stops at zero") {
    CompletedScoreInput input;
    input.total_reachable_cells = 100;

    input.actual_moves = 110;
    CHECK(compute_completed_score(input).budget_value ==
          completed_budget_award - 10 * completed_penalty_per_move_over_budget);

    // 40 moves over empties the bucket; nothing beyond that can be taken.
    input.actual_moves = 140;
    CHECK(compute_completed_score(input).budget_value == 0);
    input.actual_moves = 100000;
    const ExpeditionScore ruined = compute_completed_score(input);
    CHECK(ruined.budget_value == 0);
    CHECK(ruined.value == completed_exit_award);
}

// --- Discoveries -------------------------------------------------------------

TEST_CASE("the carried bonus multiplies the discovery award") {
    CompletedScoreInput input;
    input.total_reachable_cells = 50;
    input.discoveries_found = 2;
    input.discovery_multiplier = 2;
    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.discovery_value == 2 * completed_score_per_discovery * 2);
}

TEST_CASE("a zero multiplier is treated as the neutral one") {
    CompletedScoreInput input;
    input.total_reachable_cells = 50;
    input.discoveries_found = 3;
    input.discovery_multiplier = 0;
    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.discovery_multiplier == 1);
    CHECK(score.discovery_value == 3 * completed_score_per_discovery);
}

// --- Robustness --------------------------------------------------------------

TEST_CASE("scoring is overflow-safe at extreme counters") {
    constexpr std::uint64_t huge = std::numeric_limits<std::uint64_t>::max();
    CompletedScoreInput input;
    input.explored_reachable_cells = huge;
    input.total_reachable_cells = huge;
    input.actual_moves = huge;
    input.discoveries_found = huge;
    input.discovery_multiplier = huge;

    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.explored_percent <= 100);
    CHECK(score.exploration_value <= completed_exploration_maximum);
    CHECK(score.budget_value == completed_budget_award);  // Nothing is over budget.
    CHECK(score.value == huge);                           // Saturated, never wrapped.
}

}  // TEST_SUITE("expedition_score")
