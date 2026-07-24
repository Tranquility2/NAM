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
    MoveOutcome moved{MoveResult::moved, {0, 0}, {1, 0}, Terrain::water, 3, 12, 9};
    CHECK(describe_move(moved).find("water") != std::string::npos);

    MoveOutcome boundary{MoveResult::blocked_by_boundary, {0, 0}, {0, 0}, Terrain::open, 0, 12, 12};
    CHECK(describe_move(boundary).find("edge") != std::string::npos);

    MoveOutcome terrain{MoveResult::blocked_by_terrain, {0, 0}, {0, 0}, Terrain::wall_vertical,
                        0, 12, 12};
    CHECK(describe_move(terrain).find("Blocked") != std::string::npos);
}

TEST_CASE("successful move messages state the destination terrain and exact cost") {
    // A singular one-point cost onto open ground.
    MoveOutcome one{MoveResult::moved, {0, 0}, {1, 0}, Terrain::open, 1, 12, 11};
    CHECK(describe_move(one) == "Moved onto open ground for 1 stamina.");

    // A multi-point cost onto a mountain.
    MoveOutcome four{MoveResult::moved, {0, 0}, {1, 0}, Terrain::mountain, 4, 12, 8};
    CHECK(describe_move(four) == "Moved onto mountain for 4 stamina.");
}

TEST_CASE("insufficient-stamina messages state terrain, required cost, and available stamina") {
    // A singular one-point requirement the actor cannot meet at zero stamina.
    MoveOutcome open_block{MoveResult::blocked_by_stamina, {3, 0}, {3, 0}, Terrain::open, 1, 0, 0};
    CHECK(describe_move(open_block) == "Not enough stamina for open ground: need 1, have 0.");

    // A multi-point requirement with a non-zero remaining stamina.
    MoveOutcome mountain_block{MoveResult::blocked_by_stamina, {3, 0}, {3, 0}, Terrain::mountain,
                              4, 2, 2};
    CHECK(describe_move(mountain_block) == "Not enough stamina for mountain: need 4, have 2.");
}

TEST_CASE("boundary and impassable-terrain wording carries no stamina cost") {
    MoveOutcome boundary{MoveResult::blocked_by_boundary, {0, 0}, {0, 0}, Terrain::open, 0, 7, 7};
    CHECK(describe_move(boundary) == "Blocked by the edge of the map.");

    MoveOutcome wall{MoveResult::blocked_by_terrain, {0, 0}, {0, 0}, Terrain::wall_horizontal,
                    0, 7, 7};
    CHECK(describe_move(wall) == "Blocked by wall.");
}

TEST_CASE("rest messages state the recovered amount or that stamina is full") {
    // A positive recovery reports the exact amount.
    CHECK(describe_rest(RestedEvent{0, 4, 4}) == "Rested and recovered 4 stamina.");
    CHECK(describe_rest(RestedEvent{10, 2, 12}) == "Rested and recovered 2 stamina.");
    CHECK(describe_rest(RestedEvent{11, 1, 12}) == "Rested and recovered 1 stamina.");

    // A rest at full stamina recovers zero and reports the full-stamina message.
    CHECK(describe_rest(RestedEvent{12, 0, 12}) == "Stamina is already full.");
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
    CHECK(objective_line(objective_with(ObjectiveStatus::seeking_beacon)) ==
          "Objective: Reach Glass River Beacon (*), then return to spawn.");
    CHECK(objective_line(objective_with(ObjectiveStatus::returning_to_spawn)) ==
          "Objective: Return to spawn.");
    CHECK(objective_line(objective_with(ObjectiveStatus::completed)) ==
          "Objective complete: Glass River Beacon.");
}

TEST_CASE("the compact goal line stays short for every phase") {
    CHECK(goal_line(objective_with(ObjectiveStatus::seeking_beacon)) ==
          "Goal: reach Glass River Beacon");
    CHECK(goal_line(objective_with(ObjectiveStatus::returning_to_spawn)) ==
          "Goal: return to spawn");
    CHECK(goal_line(objective_with(ObjectiveStatus::completed)) == "Goal: complete");
}

TEST_CASE("beacon transition messages match the exact required wording") {
    CHECK(describe_beacon_discovered("Glass River Beacon") ==
          "Reached Glass River Beacon. Return to spawn.");
    CHECK(describe_expedition_completed("Glass River Beacon") ==
          "Objective complete: returned to spawn after reaching Glass River Beacon.");
    CHECK(describe_spawn_beacon("Glass River Beacon") ==
          "Objective complete: Glass River Beacon is at spawn.");
}

TEST_CASE("objective-screen reminders and restored completion wording are exact") {
    // REQ-020 / REQ-026 / REQ-028: the plain-mode reminders and the restored
    // normal-screen completion line use their exact fixed wording, and the
    // restored line names the finished beacon.
    CHECK(discovery_reminder() ==
          "Beacon discovered. Press Enter or use a movement command to continue.");
    CHECK(completion_reminder() == "Run complete. Press Enter or q to exit.");
    CHECK(restored_completion_message("Glass River Beacon") ==
          "Expedition complete: Glass River Beacon.");
    // The restored completion line carries neither the pre-completion goodbye
    // wording nor any coordinate.
    CHECK(restored_completion_message("Glass River Beacon").find("Goodbye") == std::string::npos);
}

}  // TEST_SUITE("console")
