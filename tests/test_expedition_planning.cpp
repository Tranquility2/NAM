#include <doctest/doctest.h>

#include <cstdint>
#include <string_view>
#include <variant>

#include "coordinates.h"
#include "expedition_planning.h"
#include "map.h"
#include "map_parser.h"
#include "objective.h"

namespace {

Map make_map(std::string_view text) {
    MapLoadResult result = load_map(text);
    REQUIRE(std::holds_alternative<Map>(result));
    return std::get<Map>(std::move(result));
}

// Compute the baseline for a whole map by first locating its deterministic beacon,
// then running the plan search from spawn to that beacon.
ExpeditionPlanBaseline baseline_of(std::string_view text) {
    const Map map = make_map(text);
    const BeaconObjective objective = create_beacon_objective(map, 20);
    return compute_expedition_plan_baseline(map, map.spawn(), objective.beacon, 20);
}

}  // namespace

TEST_SUITE("game") {

TEST_CASE("a beacon at spawn plans a single day with no provisions") {
    // A single reachable cell: the beacon coincides with spawn, so the round trip
    // is already complete on day 1 with no provisions (REQ-017).
    const Map map = make_map("NAM-MAP 1\nwidth 3\nheight 1\nspawn 0 0\n---\n.=.\n");
    const ExpeditionPlanBaseline baseline =
        compute_expedition_plan_baseline(map, map.spawn(), map.spawn(), 20);
    CHECK(baseline.minimum_completion_days == 1u);
    CHECK(baseline.minimum_required_provisions == 0u);
}

TEST_CASE("an open round trip that fits one day needs no provisions") {
    // Twelve open steps (six out, six back) fit exactly in a 12-hour day and spend
    // only 12 stamina, so no rest or camp is needed.
    const ExpeditionPlanBaseline baseline =
        baseline_of("NAM-MAP 1\nwidth 7\nheight 1\nspawn 0 0\n---\n.......\n");
    CHECK(baseline.minimum_completion_days == 1u);
    CHECK(baseline.minimum_required_provisions == 0u);
}

TEST_CASE("an open round trip that overflows one day needs one overnight camp") {
    // Fourteen open steps (seven out to (7,0), seven back) exceed the 12-hour day,
    // so the minimum plan uses exactly one normal camp: two days, one provision.
    const Map map = make_map("NAM-MAP 1\nwidth 8\nheight 1\nspawn 0 0\n---\n........\n");
    const ExpeditionPlanBaseline baseline =
        compute_expedition_plan_baseline(map, {0, 0}, {7, 0}, 20);
    CHECK(baseline.minimum_completion_days == 2u);
    CHECK(baseline.minimum_required_provisions == 1u);
}

TEST_CASE("travel hours differ from stamina cost so terrain choice changes the plan") {
    // A short mountain round trip: two mountain steps out and two back cost 3 hours
    // each (12 hours total) but only 16 stamina, fitting one day with no provisions.
    const ExpeditionPlanBaseline baseline =
        baseline_of("NAM-MAP 1\nwidth 3\nheight 1\nspawn 0 0\n---\n.@@\n");
    CHECK(baseline.minimum_completion_days == 1u);
    CHECK(baseline.minimum_required_provisions == 0u);
}

TEST_CASE("a fields corridor overflowing the day needs one overnight") {
    // Fields cost 1 hour but 2 stamina. Eight-cell corridor: beacon far enough that
    // the round trip overflows a single day and needs one camp.
    const ExpeditionPlanBaseline baseline =
        baseline_of("NAM-MAP 1\nwidth 9\nheight 1\nspawn 0 0\n---\n.xxxxxxxx\n");
    CHECK(baseline.minimum_completion_days == 2u);
    CHECK(baseline.minimum_required_provisions == 1u);
}

TEST_CASE("a long mountain corridor needs several overnights and many provisions") {
    // Mountains force expensive bivouacs and rests, so the minimum plan spans five
    // days and consumes nine provisions.
    const ExpeditionPlanBaseline baseline =
        baseline_of("NAM-MAP 1\nwidth 9\nheight 1\nspawn 0 0\n---\n.@@@@@@@@\n");
    CHECK(baseline.minimum_completion_days == 5u);
    CHECK(baseline.minimum_required_provisions == 9u);
}

TEST_CASE("the objective exposes the planning baseline and a deadline two days later") {
    // create_beacon_objective stores the same minimum-day baseline and derives the
    // deadline as minimum completion days plus two (REQ-019).
    const Map map = make_map("NAM-MAP 1\nwidth 8\nheight 1\nspawn 0 0\n---\n........\n");
    const BeaconObjective objective = create_beacon_objective(map, 20);
    const ExpeditionPlanBaseline baseline =
        compute_expedition_plan_baseline(map, map.spawn(), objective.beacon, 20);
    CHECK(objective.minimum_completion_days == baseline.minimum_completion_days);
    CHECK(objective.minimum_required_provisions == baseline.minimum_required_provisions);
    CHECK(objective.deadline_days == objective.minimum_completion_days + 2u);
}

TEST_CASE("the planning search is deterministic across repeated runs") {
    const std::string_view text = "NAM-MAP 1\nwidth 9\nheight 1\nspawn 0 0\n---\n.@@@@@@@@\n";
    const ExpeditionPlanBaseline first = baseline_of(text);
    const ExpeditionPlanBaseline second = baseline_of(text);
    CHECK(first.minimum_completion_days == second.minimum_completion_days);
    CHECK(first.minimum_required_provisions == second.minimum_required_provisions);
}

}  // TEST_SUITE("game")
