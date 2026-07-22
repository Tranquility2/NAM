#include <doctest/doctest.h>

#include <string>

#include "coordinates.h"
#include "direction.h"
#include "map_parser.h"
#include "messages.h"
#include "move_outcome.h"
#include "terrain.h"

using namespace nam::console;

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

}  // TEST_SUITE("console")
