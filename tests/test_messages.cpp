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
    MoveOutcome moved{MoveResult::moved, {0, 0}, {1, 0}, Terrain::water};
    CHECK(describe_move(moved).find("water") != std::string::npos);

    MoveOutcome boundary{MoveResult::blocked_by_boundary, {0, 0}, {0, 0}, Terrain::open};
    CHECK(describe_move(boundary).find("edge") != std::string::npos);

    MoveOutcome terrain{MoveResult::blocked_by_terrain, {0, 0}, {0, 0}, Terrain::wall_vertical};
    CHECK(describe_move(terrain).find("Blocked") != std::string::npos);
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
