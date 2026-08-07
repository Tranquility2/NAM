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

TEST_SUITE("messages") {

TEST_CASE("terrain names are provided for every terrain") {
    CHECK(terrain_name(Terrain::open) == "open ground");
    CHECK(terrain_name(Terrain::mountain) == "mountain");
    CHECK(terrain_name(Terrain::shallow_water) == "shallow water");
    CHECK(terrain_name(Terrain::fields) == "fields");
    CHECK(terrain_name(Terrain::hill) == "hill");
    CHECK(terrain_name(Terrain::forest) == "forest");
    CHECK(terrain_name(Terrain::deep_water) == "deep water");
    CHECK(terrain_name(Terrain::cliff) == "cliff");
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

TEST_CASE("a move onto authored content keeps the ordinary terrain wording") {
    // Content grants a one-off reveal, so it no longer changes the move line. The
    // wide reveal is announced by its own message instead.
    MoveOutcome vantage{MoveResult::moved, {0, 0}, {1, 0}, Terrain::open};
    vantage.feature = LevelFeatureKind::vantage_point;
    MoveOutcome plain{MoveResult::moved, {0, 0}, {1, 0}, Terrain::open};
    CHECK(plain.feature.has_value() == false);
    CHECK(describe_move(vantage) == describe_move(plain));

    // Each kind of viewpoint is described as itself, and each says what came
    // into view, so a player learns the vocabulary from the message alone.
    CHECK(describe_vantage_reached(VantageKind::cairn).find("cairn") != std::string::npos);
    CHECK(describe_vantage_reached(VantageKind::lookout).find("lookout") != std::string::npos);
    CHECK(describe_vantage_reached(VantageKind::summit).find("summit") != std::string::npos);
    for (const VantageKind kind : all_vantage_kinds) {
        CHECK(describe_vantage_reached(kind).find("view") != std::string::npos);
    }
}

TEST_CASE("each crossing is announced as the place it actually is") {
    const SetPieceKind kinds[] = {SetPieceKind::ford, SetPieceKind::ridge,
                                  SetPieceKind::lakeshore, SetPieceKind::high_pass};
    for (std::size_t i = 0; i < 4u; ++i) {
        CHECK(describe_set_piece_crossed(kinds[i]).empty() == false);
        for (std::size_t j = i + 1u; j < 4u; ++j) {
            CHECK(describe_set_piece_crossed(kinds[i]) != describe_set_piece_crossed(kinds[j]));
        }
    }
    CHECK(describe_set_piece_crossed(SetPieceKind::high_pass).find("pass") != std::string::npos);
}

TEST_CASE("move outcomes map to distinct human-readable sentences") {
    MoveOutcome moved{MoveResult::moved, {0, 0}, {1, 0}, Terrain::shallow_water};
    CHECK(describe_move(moved).find("water") != std::string::npos);

    MoveOutcome boundary{MoveResult::blocked_by_boundary, {0, 0}, {0, 0}, Terrain::open};
    CHECK(describe_move(boundary).find("edge") != std::string::npos);

    MoveOutcome terrain{MoveResult::blocked_by_terrain, {0, 0}, {0, 0}, Terrain::cliff};
    CHECK(describe_move(terrain).find("Blocked") != std::string::npos);
}

TEST_CASE("successful move messages state the destination terrain and its sight") {
    // Sight is the only thing terrain governs now, so it is the only number the
    // move line carries. Forest is the one terrain that sees less than baseline.
    MoveOutcome forest{MoveResult::moved, {0, 0}, {1, 0}, Terrain::forest};
    CHECK(describe_move(forest) == "Moved onto forest. Sight 1.");

    MoveOutcome open{MoveResult::moved, {0, 0}, {1, 0}, Terrain::open};
    CHECK(describe_move(open) == "Moved onto open ground. Sight 3.");

    MoveOutcome mountain{MoveResult::moved, {0, 0}, {1, 0}, Terrain::mountain};
    CHECK(describe_move(mountain) == "Moved onto mountain. Sight 7.");
}

TEST_CASE("boundary and impassable-terrain wording names no sight range") {
    MoveOutcome boundary{MoveResult::blocked_by_boundary, {0, 0}, {0, 0}, Terrain::open};
    CHECK(describe_move(boundary) == "Blocked by the edge of the map.");

    MoveOutcome cliff{MoveResult::blocked_by_terrain, {0, 0}, {0, 0}, Terrain::cliff};
    CHECK(describe_move(cliff) == "Blocked by cliff.");

    MoveOutcome deep{MoveResult::blocked_by_terrain, {0, 0}, {0, 0}, Terrain::deep_water};
    CHECK(describe_move(deep) == "Blocked by deep water.");
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

TEST_CASE("a level's opening line says what the carried bonus is worth here") {
    // Three bonuses only help if the line that announces one says what it does,
    // in this level's terms, without ever printing the core's identifier.
    const std::string plain = describe_level_started(LevelTier::small, 1, 2, ExpeditionBonus::none);
    CHECK(plain == "Small level (1 of 2).");

    std::vector<std::string> lines;
    for (const ExpeditionBonus bonus : earnable_bonuses) {
        const std::string text = describe_level_started(LevelTier::small, 2, 2, bonus);
        CHECK(text.rfind("Small level (2 of 2).", 0) == 0);
        CHECK(text.size() > plain.size());
        CHECK(text.find(std::string(to_string(bonus))) == std::string::npos);
        lines.push_back(text);
    }
    for (std::size_t left = 0; left < lines.size(); ++left) {
        for (std::size_t right = left + 1u; right < lines.size(); ++right) {
            CHECK(lines[left] != lines[right]);
        }
    }
}

}  // TEST_SUITE("messages")
