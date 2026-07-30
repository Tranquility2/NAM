#include <doctest/doctest.h>

#include <string>

#include "coordinates.h"
#include "direction.h"
#include "game_event.h"
#include "map_parser.h"
#include "level_feature.h"
#include "messages.h"
#include "move_outcome.h"
#include "objective.h"
#include "terrain.h"

using namespace nam::console;

namespace {

LevelObjective objective_with(ObjectiveStatus status) {
    LevelObjective objective;
    objective.exit_cell = Coordinates{4, 0};
    objective.name = "Glass River Exit";
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

TEST_CASE("authored content changes the move wording it deserves") {
    MoveOutcome hazard{MoveResult::moved, {0, 0}, {1, 0}, Terrain::open, 7, 0, 12, 5};
    hazard.feature = LevelFeatureKind::hazard;
    CHECK(describe_move(hazard).find("hazard") != std::string::npos);
    CHECK(describe_move(hazard).find("7 stamina") != std::string::npos);

    MoveOutcome safe{MoveResult::moved, {0, 0}, {1, 0}, Terrain::open, 1, 9, 12, 20};
    safe.feature = LevelFeatureKind::safe_landmark;
    CHECK(describe_move(safe).find("safe landmark") != std::string::npos);

    MoveOutcome plain{MoveResult::moved, {0, 0}, {1, 0}, Terrain::open, 1, 2, 12, 13};
    CHECK(plain.feature.has_value() == false);
    CHECK(describe_move(plain).find("hazard") == std::string::npos);
    CHECK(describe_move(plain).find("safe landmark") == std::string::npos);

    CHECK(describe_discovery_found(1, 1).find("1 of 1") != std::string::npos);
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

TEST_CASE("successful move messages state the destination terrain and cost") {
    // A one-point cost onto open ground, whose passive recovery is reported
    // alongside the charge.
    MoveOutcome one{MoveResult::moved, {0, 0}, {1, 0}, Terrain::open, 1, 2, 12, 13};
    CHECK(describe_move(one) == "Moved onto open ground for 1 stamina. Recovered 2 stamina.");

    // A multi-point step onto a mountain, which gives nothing back and therefore
    // reports no recovery.
    MoveOutcome four{MoveResult::moved, {0, 0}, {1, 0}, Terrain::mountain, 4, 0, 12, 8};
    CHECK(describe_move(four) == "Moved onto mountain for 4 stamina.");
}

TEST_CASE("a fully restoring step reports the recovery it granted") {
    // Reaching the landmark refills the meter, so the recovery dwarfs the charge.
    MoveOutcome landmark{MoveResult::moved, {3, 0}, {4, 0}, Terrain::hill, 2, 8, 14, 20};
    CHECK(describe_move(landmark) == "Moved onto hill for 2 stamina. Recovered 8 stamina.");
}

TEST_CASE("boundary and impassable-terrain wording carries no stamina cost") {
    MoveOutcome boundary{MoveResult::blocked_by_boundary, {0, 0}, {0, 0}, Terrain::open,
                         0, 0, 7, 7};
    CHECK(describe_move(boundary) == "Blocked by the edge of the map.");

    MoveOutcome wall{MoveResult::blocked_by_terrain, {0, 0}, {0, 0}, Terrain::wall_horizontal,
                    0, 0, 7, 7};
    CHECK(describe_move(wall) == "Blocked by wall.");
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
          "Objective: Reach Glass River Exit (*).");
    CHECK(objective_line(objective_with(ObjectiveStatus::seeking_exit)) ==
          "Objective: Reach the exit to the east (*).");
    CHECK(objective_line(objective_with(ObjectiveStatus::completed)) ==
          "Level complete: reached the exit beyond Glass River Exit.");
}

TEST_CASE("the compact goal line stays short for every phase") {
    CHECK(goal_line(objective_with(ObjectiveStatus::seeking_landmark)) ==
          "Goal: reach Glass River Exit");
    CHECK(goal_line(objective_with(ObjectiveStatus::seeking_exit)) == "Goal: exit east");
    CHECK(goal_line(objective_with(ObjectiveStatus::completed)) == "Goal: complete");
}

TEST_CASE("exit_cell transition messages match the exact required wording") {
    CHECK(describe_landmark_discovered("Glass River Exit") ==
          "Reached Glass River Exit. Exit direction revealed.");
    CHECK(describe_level_completed("Glass River Exit") ==
          "Level complete: reached the exit after Glass River Exit.");
    CHECK(describe_spawn_landmark("Glass River Exit") ==
          "Level complete: Glass River Exit and the exit are at spawn.");
}

TEST_CASE("objective-screen reminders and restored completion wording are exact") {
    // REQ-020 / REQ-026 / REQ-028: the plain-mode reminders and the restored
    // normal-screen completion line use their exact fixed wording, and the
    // restored line names the finished level.
    CHECK(discovery_reminder() ==
          "Landmark discovered. Press Enter or use a movement command to continue.");
    CHECK(completion_reminder() == "Run complete. Press Enter or q to exit.");
    CHECK(restored_completion_message("Glass River Exit") ==
          "Level complete beyond Glass River Exit.");
    // The restored completion line carries neither the pre-completion goodbye
    // wording nor any coordinate.
    CHECK(restored_completion_message("Glass River Exit").find("Goodbye") == std::string::npos);
}

}  // TEST_SUITE("console")
