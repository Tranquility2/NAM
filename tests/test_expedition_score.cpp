#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
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

// An independent Dijkstra over walkable cardinal neighbours with edge weight
// stamina_cost_of(destination), mirroring the objective's optimal-cost baseline
// so the test never trusts the implementation's own traversal.
std::uint64_t independent_travel_cost(const Map& map, Coordinates source, Coordinates target) {
    constexpr std::uint64_t unreachable = std::numeric_limits<std::uint64_t>::max();
    const int width = static_cast<int>(map.width());
    const int height = static_cast<int>(map.height());
    const auto flat = [width](Coordinates c) {
        return static_cast<std::size_t>(c.y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(c.x);
    };
    std::vector<std::uint64_t> best(static_cast<std::size_t>(width) *
                                        static_cast<std::size_t>(height),
                                    unreachable);
    std::vector<bool> done(best.size(), false);
    best[flat(source)] = 0;
    const std::array<Coordinates, 4> offsets{Coordinates{0, -1}, Coordinates{0, 1},
                                             Coordinates{-1, 0}, Coordinates{1, 0}};
    for (std::size_t step = 0; step < best.size(); ++step) {
        std::size_t current = best.size();
        std::uint64_t current_cost = unreachable;
        for (std::size_t i = 0; i < best.size(); ++i) {
            if (!done[i] && best[i] < current_cost) {
                current_cost = best[i];
                current = i;
            }
        }
        if (current == best.size()) {
            break;
        }
        done[current] = true;
        const Coordinates here{static_cast<int>(current % static_cast<std::size_t>(width)),
                               static_cast<int>(current / static_cast<std::size_t>(width))};
        for (const Coordinates offset : offsets) {
            const Coordinates neighbour = here + offset;
            if (!map.contains(neighbour)) {
                continue;
            }
            const std::optional<std::uint32_t> weight = stamina_cost_of(map.terrain_at(neighbour));
            if (!weight.has_value()) {
                continue;
            }
            const std::size_t ni = flat(neighbour);
            const std::uint64_t candidate = current_cost + static_cast<std::uint64_t>(*weight);
            if (candidate < best[ni]) {
                best[ni] = candidate;
            }
        }
    }
    return best[flat(target)];
}

}  // namespace

TEST_SUITE("game") {

// --- Objective round-trip baseline (unchanged golden behavior) --------------

TEST_CASE("the objective stores the exact cheapest round-trip terrain-entry cost") {
    const Map map = make_map(
        "NAM-MAP 1\nwidth 9\nheight 5\nspawn 1 2\n---\n"
        ".........\n.........\n.........\n.........\n.........\n");
    const BeaconObjective objective = create_beacon_objective(map);
    const std::uint64_t outbound = independent_travel_cost(map, map.spawn(), objective.beacon);
    const std::uint64_t inbound = independent_travel_cost(map, objective.beacon, map.spawn());
    CHECK(objective.minimum_round_trip_stamina_cost == outbound + inbound);
}

TEST_CASE("asymmetric terrain-entry costs make the two legs differ") {
    const Map map = make_map(
        "NAM-MAP 1\nwidth 7\nheight 3\nspawn 0 1\n---\n"
        "=======\n.~@x^@.\n=======\n");
    const BeaconObjective objective = create_beacon_objective(map);
    const std::uint64_t outbound = independent_travel_cost(map, map.spawn(), objective.beacon);
    const std::uint64_t inbound = independent_travel_cost(map, objective.beacon, map.spawn());
    CHECK(outbound != inbound);
    CHECK(objective.minimum_round_trip_stamina_cost == outbound + inbound);
}

TEST_CASE("a single reachable spawn yields a zero-cost round trip") {
    const Map map = make_map("NAM-MAP 1\nwidth 3\nheight 3\nspawn 1 1\n---\n===\n=.=\n===\n");
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.status == ObjectiveStatus::completed);
    CHECK(objective.beacon == map.spawn());
    CHECK(objective.minimum_round_trip_stamina_cost == 0);
}

TEST_CASE("the round-trip cost never mutates beacon placement or naming") {
    const Map map = make_map(
        "NAM-MAP 1\nwidth 9\nheight 5\nspawn 4 2\n---\n"
        ".........\n....^....\n....@....\n....^....\n.........\n");
    const BeaconObjective a = create_beacon_objective(map);
    const BeaconObjective b = create_beacon_objective(map);
    CHECK(a.beacon == b.beacon);
    CHECK(a.name == b.name);
    CHECK(a.minimum_round_trip_stamina_cost == b.minimum_round_trip_stamina_cost);
    CHECK(a.minimum_round_trip_stamina_cost > 0);
}

// --- Terrain rest recovery --------------------------------------------------

TEST_CASE("terrain rest recovery is the centralized single source of truth") {
    CHECK(rest_recovery_of(Terrain::fields) == std::optional<std::uint32_t>(6));
    CHECK(rest_recovery_of(Terrain::open) == std::optional<std::uint32_t>(4));
    CHECK(rest_recovery_of(Terrain::hill) == std::optional<std::uint32_t>(3));
    CHECK(rest_recovery_of(Terrain::mountain) == std::optional<std::uint32_t>(2));
    CHECK(rest_recovery_of(Terrain::water) == std::optional<std::uint32_t>(1));
    CHECK_FALSE(rest_recovery_of(Terrain::wall_horizontal).has_value());
    CHECK_FALSE(rest_recovery_of(Terrain::wall_vertical).has_value());
}

// --- Minimum provisions and reachability ------------------------------------

TEST_CASE("a single-cell objective requires no provisions") {
    const Map map = make_map("NAM-MAP 1\nwidth 3\nheight 3\nspawn 1 1\n---\n===\n=.=\n===\n");
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.minimum_required_provisions == 0);
    CHECK(objective.total_reachable_walkable_cells == 1);
}

TEST_CASE("a round trip within one stamina bar requires no provisions") {
    // Spawn at one end of a short open corridor: the whole round trip fits inside
    // the stamina cap of 20, so no rest and no provision are ever needed.
    const Map map = make_map("NAM-MAP 1\nwidth 5\nheight 3\nspawn 0 1\n---\n=====\n.....\n=====\n");
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.minimum_round_trip_stamina_cost == 8);
    CHECK(objective.minimum_required_provisions == 0);
    CHECK(objective.total_reachable_walkable_cells == 5);
}

TEST_CASE("a round trip that exceeds one stamina bar requires exactly one provision") {
    // Spawn 0 1; a hill at (10,1) is the unique scenic beacon. The round trip
    // costs 21 stamina (11 out including the hill, 10 back), which is just over the
    // cap of 20, so exactly one open-ground rest is required.
    const Map map = make_map(
        "NAM-MAP 1\nwidth 12\nheight 3\nspawn 0 1\n---\n"
        "============\n..........^.\n============\n");
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.beacon == Coordinates{10, 1});  // unique scenic hill
    CHECK(objective.minimum_round_trip_stamina_cost == 21);
    CHECK(objective.minimum_required_provisions == 1);
}

TEST_CASE("a game starts with the minimum provisions plus one spare") {
    const Map map = make_map(
        "NAM-MAP 1\nwidth 12\nheight 3\nspawn 0 1\n---\n"
        "============\n..........^.\n============\n");
    GameState state{Map(map)};
    CHECK(state.starting_provisions() == state.objective().minimum_required_provisions + 1);
    CHECK(state.provisions() == state.starting_provisions());
}

TEST_CASE("the minimum-provision search is deterministic across repeated builds") {
    const Map map = make_map(
        "NAM-MAP 1\nwidth 12\nheight 3\nspawn 0 1\n---\n"
        "============\n..........^.\n============\n");
    const BeaconObjective a = create_beacon_objective(map);
    const BeaconObjective b = create_beacon_objective(map);
    CHECK(a.minimum_required_provisions == b.minimum_required_provisions);
    CHECK(a.total_reachable_walkable_cells == b.total_reachable_walkable_cells);
}

TEST_CASE("exploration counts reflect reachable walkable cells and explored memory") {
    const Map map = make_map("NAM-MAP 1\nwidth 5\nheight 3\nspawn 0 1\n---\n=====\n.....\n=====\n");
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

// --- Completed-run scoring --------------------------------------------------

TEST_CASE("an optimal completed run with the spare unused scores the maximum") {
    CompletedScoreInput input;
    input.optimal_round_trip_cost = 8;
    input.actual_stamina_spent = 8;
    input.blocked_attempts = 0;
    input.provisions_remaining = 1;  // The one spare provision is unused.
    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.result == ExpeditionResult::completed);
    CHECK(score.spare_unused);
    CHECK(score.excess_stamina == 0);
    CHECK(score.value == 1000);
}

TEST_CASE("a completed run that used the spare provision scores the base") {
    CompletedScoreInput input;
    input.optimal_round_trip_cost = 8;
    input.actual_stamina_spent = 8;
    input.provisions_remaining = 0;  // The spare was spent.
    const ExpeditionScore score = compute_completed_score(input);
    CHECK_FALSE(score.spare_unused);
    CHECK(score.value == 900);
}

TEST_CASE("each excess stamina point subtracts five from a completed run") {
    CompletedScoreInput input;
    input.optimal_round_trip_cost = 10;
    input.actual_stamina_spent = 15;  // Five points of excess stamina.
    input.provisions_remaining = 1;
    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.excess_stamina == 5);
    CHECK(score.value == 900 + 100 - 5 * 5);
}

TEST_CASE("each blocked move subtracts twenty from a completed run") {
    CompletedScoreInput input;
    input.optimal_round_trip_cost = 5;
    input.actual_stamina_spent = 5;
    input.blocked_attempts = 3;
    input.provisions_remaining = 0;
    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.blocked_attempts == 3);
    CHECK(score.value == 900 - 3 * 20);
}

TEST_CASE("completed penalties accumulate and clamp at zero") {
    CompletedScoreInput input;
    input.optimal_round_trip_cost = 0;
    input.actual_stamina_spent = 400;  // 400 excess * 5 = 2000.
    input.blocked_attempts = 10;       // 200 more.
    input.provisions_remaining = 1;
    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.value == 0);
}

TEST_CASE("completed scoring is overflow-safe at extreme counters") {
    CompletedScoreInput input;
    input.optimal_round_trip_cost = 0;
    input.actual_stamina_spent = std::numeric_limits<std::uint64_t>::max();
    input.blocked_attempts = std::numeric_limits<std::uint64_t>::max();
    input.provisions_remaining = 0;
    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.value == 0);
}

// --- Rescued-run scoring ----------------------------------------------------

TEST_CASE("rescued exploration is a floored fraction of reachable cells") {
    RescuedScoreInput input;
    input.explored_reachable_cells = 4;
    input.total_reachable_cells = 5;  // floor(500 * 4 / 5) = 400.
    const ExpeditionScore score = compute_rescued_score(input);
    CHECK(score.result == ExpeditionResult::rescued);
    CHECK(score.exploration_points == 400);
    CHECK(score.value == 400);
}

TEST_CASE("a discovered beacon adds two hundred fifty to a rescued run") {
    RescuedScoreInput input;
    input.explored_reachable_cells = 4;
    input.total_reachable_cells = 5;
    input.beacon_discovered = true;
    const ExpeditionScore score = compute_rescued_score(input);
    CHECK(score.value == 400 + 250);
}

TEST_CASE("each blocked move subtracts twenty from a rescued run") {
    RescuedScoreInput input;
    input.explored_reachable_cells = 4;
    input.total_reachable_cells = 5;
    input.beacon_discovered = true;
    input.blocked_attempts = 3;
    const ExpeditionScore score = compute_rescued_score(input);
    CHECK(score.value == 400 + 250 - 3 * 20);
}

TEST_CASE("a fully explored rescued run clamps at the rescued maximum") {
    RescuedScoreInput input;
    input.explored_reachable_cells = 8;
    input.total_reachable_cells = 8;  // 500 exploration.
    input.beacon_discovered = true;   // + 250 = 750, the rescued maximum.
    const ExpeditionScore score = compute_rescued_score(input);
    CHECK(score.exploration_points == 500);
    CHECK(score.value == 750);
}

TEST_CASE("a zero-denominator rescued run scores no exploration") {
    RescuedScoreInput input;
    input.explored_reachable_cells = 0;
    input.total_reachable_cells = 0;
    const ExpeditionScore score = compute_rescued_score(input);
    CHECK(score.exploration_points == 0);
    CHECK(score.value == 0);
}

TEST_CASE("rescued scoring is overflow-safe at extreme counters") {
    RescuedScoreInput input;
    input.explored_reachable_cells = std::numeric_limits<std::uint64_t>::max();
    input.total_reachable_cells = 1;  // Exploration clamps to the maximum.
    input.beacon_discovered = true;
    input.blocked_attempts = std::numeric_limits<std::uint64_t>::max();
    const ExpeditionScore score = compute_rescued_score(input);
    CHECK(score.value == 0);  // The blocked penalty saturates past the reward.
}

TEST_CASE("completed scoring subtracts twenty-five points per day beyond the minimum") {
    CompletedScoreInput input;
    input.provisions_remaining = 1;         // spare unused: base 900 + 100 = 1000.
    input.days_used = 4;
    input.minimum_completion_days = 1;      // 3 excess days * 25 = 75 penalty.
    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.excess_days == 3u);
    CHECK(score.value == 925u);  // 1000 - 75.
}

TEST_CASE("completed scoring applies no day penalty when finishing on time") {
    CompletedScoreInput input;
    input.provisions_remaining = 1;
    input.days_used = 2;
    input.minimum_completion_days = 2;  // exactly on time: no excess days.
    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.excess_days == 0u);
    CHECK(score.value == 1000u);
}

TEST_CASE("completed day penalty is overflow-safe and never underflows the score") {
    CompletedScoreInput input;
    input.provisions_remaining = 0;  // no spare bonus.
    input.days_used = std::numeric_limits<std::uint64_t>::max();
    input.minimum_completion_days = 1;
    const ExpeditionScore score = compute_completed_score(input);
    CHECK(score.value == 0u);  // the day penalty saturates past the base.
}

TEST_CASE("overdue scoring uses the exact exploration plus beacon formula") {
    OverdueScoreInput input;
    input.explored_reachable_cells = 5;
    input.total_reachable_cells = 10;  // floor(500 * 5 / 10) = 250.
    input.beacon_discovered = true;    // + 100 = 350.
    input.blocked_attempts = 2;        // - 40 = 310.
    const ExpeditionScore score = compute_overdue_score(input);
    CHECK(score.result == ExpeditionResult::overdue);
    CHECK(score.exploration_points == 250u);
    CHECK(score.value == 310u);
}

TEST_CASE("a fully explored overdue run clamps at the overdue maximum of 600") {
    OverdueScoreInput input;
    input.explored_reachable_cells = 8;
    input.total_reachable_cells = 8;  // 500 exploration.
    input.beacon_discovered = true;   // + 100 = 600, the overdue maximum.
    const ExpeditionScore score = compute_overdue_score(input);
    CHECK(score.exploration_points == 500u);
    CHECK(score.value == 600u);
}

TEST_CASE("overdue scoring is overflow-safe at extreme counters") {
    OverdueScoreInput input;
    input.explored_reachable_cells = std::numeric_limits<std::uint64_t>::max();
    input.total_reachable_cells = 1;
    input.beacon_discovered = true;
    input.blocked_attempts = std::numeric_limits<std::uint64_t>::max();
    const ExpeditionScore score = compute_overdue_score(input);
    CHECK(score.value == 0u);
}

}  // TEST_SUITE("game")
