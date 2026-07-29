#include <doctest/doctest.h>

#include <string>

#include "coordinates.h"
#include "direction.h"
#include "game_event.h"
#include "map_parser.h"
#include "messages.h"
#include "move_outcome.h"
#include "objective.h"
#include "terrain.h"

using namespace nam::console;

namespace {

BeaconObjective objective_with(ObjectiveStatus status) {
    BeaconObjective objective;
    objective.beacon = Coordinates{4, 0};
    objective.name = "Glass River Beacon";
    objective.status = status;
    return objective;
}

}  // namespace

TEST_SUITE("console") {

TEST_CASE("terrain names are provided for every terrain") {
    CHECK(terrain_name(Terrain::open) == "open ground");
    CHECK(terrain_name(Terrain::mountain) == "mountain");
    CHECK(terrain_name(Terrain::water) == "water");
    CHECK(terrain_name(Terrain::fields) == "fields");
    CHECK(terrain_name(Terrain::hill) == "hill");
    CHECK(terrain_name(Terrain::wall_horizontal) == "wall");
    CHECK(terrain_name(Terrain::wall_vertical) == "wall");
}

TEST_CASE("direction letters and names are consistent") {
    CHECK(direction_letter(Direction::up) == 'U');
    CHECK(direction_letter(Direction::down) == 'D');
    CHECK(direction_letter(Direction::left) == 'L');
    CHECK(direction_letter(Direction::right) == 'R');

    CHECK(direction_name(Direction::up) == "up");
    CHECK(direction_name(Direction::down) == "down");
    CHECK(direction_name(Direction::left) == "left");
    CHECK(direction_name(Direction::right) == "right");
}

TEST_CASE("move outcomes map to distinct human-readable sentences") {
    MoveOutcome moved{MoveResult::moved, {0, 0}, {1, 0}, Terrain::water, 3, 0, 12, 9};
    CHECK(describe_move(moved).find("water") != std::string::npos);

    MoveOutcome boundary{MoveResult::blocked_by_boundary, {0, 0}, {0, 0}, Terrain::open,
                         0, 0, 12, 12};
    CHECK(describe_move(boundary).find("edge") != std::string::npos);

    MoveOutcome terrain{MoveResult::blocked_by_terrain, {0, 0}, {0, 0}, Terrain::wall_vertical,
                        0, 0, 12, 12};
    CHECK(describe_move(terrain).find("Blocked") != std::string::npos);
}

TEST_CASE("successful move messages state the destination terrain cost and hours") {
    // A singular one-point cost and one hour onto open ground, whose passive
    // recovery is reported alongside the charge.
    MoveOutcome one{MoveResult::moved, {0, 0}, {1, 0}, Terrain::open, 1, 2, 12, 13, 1};
    CHECK(describe_move(one) ==
          "Moved onto open ground for 1 stamina and 1 hour. Recovered 2 stamina.");

    // A multi-point cost and multi-hour step onto a mountain, which gives nothing
    // back and therefore reports no recovery.
    MoveOutcome four{MoveResult::moved, {0, 0}, {1, 0}, Terrain::mountain, 4, 0, 12, 8, 3};
    CHECK(describe_move(four) == "Moved onto mountain for 4 stamina and 3 hours.");
}

TEST_CASE("a fully restoring step reports the recovery it granted") {
    // Reaching the landmark refills the meter, so the recovery dwarfs the charge.
    MoveOutcome landmark{MoveResult::moved, {3, 0}, {4, 0}, Terrain::hill, 2, 8, 14, 20, 2};
    CHECK(describe_move(landmark) ==
          "Moved onto hill for 2 stamina and 2 hours. Recovered 8 stamina.");
}

TEST_CASE("boundary and impassable-terrain wording carries no stamina cost") {
    MoveOutcome boundary{MoveResult::blocked_by_boundary, {0, 0}, {0, 0}, Terrain::open,
                         0, 0, 7, 7};
    CHECK(describe_move(boundary) == "Blocked by the edge of the map.");

    MoveOutcome wall{MoveResult::blocked_by_terrain, {0, 0}, {0, 0}, Terrain::wall_horizontal,
                    0, 0, 7, 7};
    CHECK(describe_move(wall) == "Blocked by wall.");
}

TEST_CASE("rest messages match the recovered full daylight-blocked and no-provisions wording") {
    CHECK(describe_rest(RestedEvent{RestResult::recovered, Terrain::fields, 10, 12, 2, 3, 2}) ==
          "Made emergency camp on fields and recovered 2 stamina. Provisions left: 2.");
    CHECK(describe_rest(
              RestedEvent{RestResult::already_full, Terrain::open, 20, 20, 0, 2, 2}) ==
          "A heroic rest is attempted. Your stamina remains heroically full.");
    CHECK(describe_rest(
              RestedEvent{RestResult::blocked_by_daylight, Terrain::open, 7, 7, 0, 2, 2}) ==
          "Too little daylight remains to rest. Make camp or press on.");
    CHECK(describe_rest(
              RestedEvent{RestResult::no_provisions, Terrain::mountain, 7, 7, 0, 0, 0}) ==
          "No provisions left to rest. Stamina holds at 7.");
}

TEST_CASE("camp messages distinguish normal camp bivouac and failure wording") {
    CampedEvent normal;
    normal.result = CampResult::camped;
    normal.kind = CampKind::normal;
    normal.terrain = Terrain::fields;
    normal.stamina_after = 20;
    normal.provisions_after = 2;
    normal.time.after.day = 3;
    CHECK(describe_camp(normal) ==
          "Made camp on fields. Stamina restored to 20, provisions 2. Day 3 begins.");

    CampedEvent bivouac;
    bivouac.result = CampResult::camped;
    bivouac.kind = CampKind::bivouac;
    bivouac.terrain = Terrain::water;
    bivouac.stamina_after = 10;
    bivouac.provisions_after = 1;
    bivouac.time.after.day = 2;
    CHECK(describe_camp(bivouac) ==
          "Bivouacked on water. A rough night: stamina 10, provisions 1. Day 2 begins.");

    CampedEvent ineligible;
    ineligible.result = CampResult::ineligible;
    CHECK(describe_camp(ineligible) == "Too early to camp. Travel a while or tire first.");

    CampedEvent broke;
    broke.result = CampResult::no_provisions;
    broke.kind = CampKind::normal;
    broke.provision_cost = 1;
    broke.provisions_before = 0;
    CHECK(describe_camp(broke) == "Not enough provisions to make camp: need 1, have 0.");
}

TEST_CASE("map errors describe the source and position when present") {
    MapLoadError error;
    error.code = MapLoadErrorCode::unknown_symbol;
    error.source = "level.map";
    error.line = 7;
    error.column = 3;
    error.message = "unknown terrain symbol 'Z'";

    const std::string text = describe_map_error(error);
    CHECK(text.find("level.map") != std::string::npos);
    CHECK(text.find("line 7") != std::string::npos);
    CHECK(text.find("column 3") != std::string::npos);
    CHECK(text.find("unknown terrain symbol") != std::string::npos);
}

TEST_CASE("map errors omit position details that do not apply") {
    MapLoadError error;
    error.code = MapLoadErrorCode::empty_input;
    error.source = "level.map";
    error.line = 0;  // not applicable
    error.column = 0;

    const std::string text = describe_map_error(error);
    CHECK(text.find("line") == std::string::npos);
    CHECK(text.find("column") == std::string::npos);
}

TEST_CASE("the objective line states the exact wording for every phase") {
    CHECK(objective_line(objective_with(ObjectiveStatus::seeking_landmark)) ==
          "Objective: Reach Glass River Beacon (*).");
    CHECK(objective_line(objective_with(ObjectiveStatus::seeking_exit)) ==
          "Objective: Reach the exit to the east (*).");
    CHECK(objective_line(objective_with(ObjectiveStatus::completed)) ==
          "Level complete: reached the exit beyond Glass River Beacon.");
}

TEST_CASE("the compact goal line stays short for every phase") {
    CHECK(goal_line(objective_with(ObjectiveStatus::seeking_landmark)) ==
          "Goal: reach Glass River Beacon");
    CHECK(goal_line(objective_with(ObjectiveStatus::seeking_exit)) == "Goal: exit east");
    CHECK(goal_line(objective_with(ObjectiveStatus::completed)) == "Goal: complete");
}

TEST_CASE("beacon transition messages match the exact required wording") {
    CHECK(describe_beacon_discovered("Glass River Beacon") ==
          "Reached Glass River Beacon. Exit direction revealed.");
    CHECK(describe_expedition_completed("Glass River Beacon") ==
          "Level complete: reached the exit after Glass River Beacon.");
    CHECK(describe_spawn_beacon("Glass River Beacon") ==
          "Level complete: Glass River Beacon and the exit are at spawn.");
}

TEST_CASE("objective-screen reminders and restored completion wording are exact") {
    // REQ-020 / REQ-026 / REQ-028: the plain-mode reminders and the restored
    // normal-screen completion line use their exact fixed wording, and the
    // restored line names the finished beacon.
    CHECK(discovery_reminder() ==
          "Landmark discovered. Press Enter or use a movement command to continue.");
    CHECK(completion_reminder() == "Run complete. Press Enter or q to exit.");
    CHECK(restored_completion_message("Glass River Beacon") ==
          "Level complete beyond Glass River Beacon.");
    CHECK(restored_rescue_message("Glass River Beacon") ==
          "Rescued: the Glass River Beacon expedition ran out of provisions and ended early.");
    CHECK(restored_overdue_message("Glass River Beacon") ==
          "Overdue: the Glass River Beacon route missed its level deadline and was collected late.");
    // The restored completion line carries neither the pre-completion goodbye
    // wording nor any coordinate.
    CHECK(restored_completion_message("Glass River Beacon").find("Goodbye") == std::string::npos);
}

}  // TEST_SUITE("console")
