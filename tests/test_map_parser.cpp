#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <variant>

#include "coordinates.h"
#include "map.h"
#include "map_parser.h"
#include "terrain.h"

namespace {

std::string fixture(std::string_view name) {
    return std::string(NAM_FIXTURES_DIR) + "/" + std::string(name);
}

// These return by value: callers frequently pass a temporary MapLoadResult, so
// returning a reference into it would dangle. A returned value bound to `const
// T&` at the call site has its lifetime extended for the whole test body.
Map expect_map(const MapLoadResult& result) {
    // Surface the parser's own message when a fixture unexpectedly fails.
    if (const auto* error = std::get_if<MapLoadError>(&result)) {
        INFO("unexpected parse error: " << error->message);
        REQUIRE(false);
    }
    return std::get<Map>(result);
}

MapLoadError expect_error(const MapLoadResult& result) {
    REQUIRE(std::holds_alternative<MapLoadError>(result));
    return std::get<MapLoadError>(result);
}

}  // namespace

TEST_SUITE("parser") {

TEST_CASE("the built-in map parses and is well formed") {
    const Map map = builtin_map();
    CHECK(map.width() == 29);
    CHECK(map.height() == 10);
    CHECK(map.spawn() == Coordinates{14, 5});
    // The spawn is inside the bounds and on walkable terrain by construction.
    REQUIRE(map.contains(map.spawn()));
    CHECK(is_walkable(map.terrain_at(map.spawn())));

    // The embedded source parses through the public entry point too.
    const MapLoadResult result = load_map(std::string_view{builtin_map_source()}, "<builtin>");
    CHECK(std::holds_alternative<Map>(result));
}

TEST_CASE("a canonical NAM-MAP 1 file parses with the declared geometry") {
    const MapLoadResult result = load_map_file(fixture("valid-lf.map"));
    const Map& map = expect_map(result);
    CHECK(map.width() == 8);
    CHECK(map.height() == 4);
    CHECK(map.spawn() == Coordinates{3, 2});
    // Corner and interior terrain: border is a wall, interior has mixed terrain.
    CHECK(map.terrain_at({0, 0}) == Terrain::wall_horizontal);
    CHECK(map.terrain_at({0, 1}) == Terrain::wall_vertical);
    CHECK(map.terrain_at({3, 1}) == Terrain::mountain);
    CHECK(map.terrain_at({4, 1}) == Terrain::water);
    CHECK(map.terrain_at({5, 1}) == Terrain::fields);
    CHECK(map.terrain_at({2, 2}) == Terrain::hill);
}

TEST_CASE("LF and CRLF encodings of the same map parse identically") {
    const MapLoadResult lf = load_map_file(fixture("valid-lf.map"));
    const MapLoadResult crlf = load_map_file(fixture("valid-crlf.map"));
    const Map& a = expect_map(lf);
    const Map& b = expect_map(crlf);

    CHECK(a.width() == b.width());
    CHECK(a.height() == b.height());
    CHECK(a.spawn() == b.spawn());
    // Full-terrain equality: the CR bytes must never survive as cells.
    CHECK(a.to_string() == b.to_string());
}

TEST_CASE("LF and CRLF strings are equivalent through the string_view entry") {
    const std::string_view body =
        "NAM-MAP 1\nwidth 3\nheight 3\nspawn 1 1\n---\n...\n...\n...\n";
    std::string crlf;
    for (const char c : body) {
        if (c == '\n') {
            crlf.push_back('\r');
        }
        crlf.push_back(c);
    }
    const Map& a = expect_map(load_map(body));
    const Map& b = expect_map(load_map(std::string_view{crlf}));
    CHECK(a.to_string() == b.to_string());
    CHECK(a.spawn() == b.spawn());
}

TEST_CASE("a legacy '<width> <height>' header parses with a deterministic spawn") {
    const MapLoadResult result = load_map_file(fixture("legacy.map"));
    const Map& map = expect_map(result);
    CHECK(map.width() == 8);
    CHECK(map.height() == 4);
    // Deterministic policy: nearest walkable cell to the centre (4,2).
    CHECK(map.spawn() == Coordinates{4, 2});
    CHECK(is_walkable(map.terrain_at(map.spawn())));
}

TEST_CASE("the deterministic spawn policy is stable across repeated parses") {
    const std::string_view text = "8 4\n========\n|..@~x.|\n|.^....|\n========\n";
    const Coordinates first = expect_map(load_map(text)).spawn();
    const Coordinates second = expect_map(load_map(text)).spawn();
    CHECK(first == second);
    CHECK(first == Coordinates{4, 2});
}

TEST_CASE("empty or whitespace-only input is rejected") {
    CHECK(expect_error(load_map(std::string_view{""})).code == MapLoadErrorCode::empty_input);
    CHECK(expect_error(load_map(std::string_view{"   \n\t\n"})).code ==
          MapLoadErrorCode::empty_input);
    CHECK(expect_error(load_map_file(fixture("empty.map"))).code ==
          MapLoadErrorCode::empty_input);
}

TEST_CASE("a truncated map reports missing rows") {
    const MapLoadError& error = expect_error(load_map_file(fixture("truncated.map")));
    CHECK(error.code == MapLoadErrorCode::missing_rows);
    CHECK(error.message.find("expected") != std::string::npos);
}

TEST_CASE("oversized dimensions are rejected before any allocation") {
    const MapLoadError& error = expect_error(load_map_file(fixture("oversized.map")));
    CHECK(error.code == MapLoadErrorCode::dimensions_out_of_range);
}

TEST_CASE("an unknown terrain symbol is reported with a 1-based line and column") {
    const MapLoadError& error = expect_error(load_map_file(fixture("unknown-terrain.map")));
    CHECK(error.code == MapLoadErrorCode::unknown_symbol);
    CHECK(error.line == 7);
    CHECK(error.column == 3);
}

TEST_CASE("a row whose width differs from the header is reported with a position") {
    const MapLoadError& error = expect_error(load_map_file(fixture("row-width-mismatch.map")));
    CHECK(error.code == MapLoadErrorCode::row_width_mismatch);
    CHECK(error.line == 7);
    CHECK(error.column == 5);
}

TEST_CASE("a first line that is neither header form is malformed") {
    const MapLoadError& error = expect_error(load_map_file(fixture("malformed-header.map")));
    CHECK(error.code == MapLoadErrorCode::malformed_header);
    CHECK(error.line == 1);
    CHECK(error.column == 1);
}

TEST_CASE("an unsupported canonical version is rejected") {
    const MapLoadError& error =
        expect_error(load_map(std::string_view{"NAM-MAP 2\nwidth 3\nheight 1\n---\n...\n"}));
    CHECK(error.code == MapLoadErrorCode::unsupported_version);
    CHECK(error.line == 1);
}

TEST_CASE("missing canonical dimensions are reported") {
    const MapLoadError& error =
        expect_error(load_map(std::string_view{"NAM-MAP 1\nwidth 3\n---\n...\n"}));
    CHECK(error.code == MapLoadErrorCode::missing_dimensions);
}

TEST_CASE("non-numeric dimensions are rejected in both header forms") {
    CHECK(expect_error(load_map(std::string_view{"NAM-MAP 1\nwidth x\nheight 1\n---\n.\n"})).code ==
          MapLoadErrorCode::invalid_dimensions);
    CHECK(expect_error(load_map(std::string_view{"8 x\n........\n"})).code ==
          MapLoadErrorCode::invalid_dimensions);
}

TEST_CASE("more than one spawn line is a duplicate error") {
    const MapLoadError& error = expect_error(load_map(std::string_view{
        "NAM-MAP 1\nwidth 3\nheight 1\nspawn 0 0\nspawn 1 0\n---\n...\n"}));
    CHECK(error.code == MapLoadErrorCode::duplicate_spawn);
}

TEST_CASE("an explicit spawn is validated for bounds and walkability") {
    CHECK(expect_error(load_map_file(fixture("spawn-out-of-bounds.map"))).code ==
          MapLoadErrorCode::spawn_out_of_bounds);
    CHECK(expect_error(load_map_file(fixture("spawn-not-walkable.map"))).code ==
          MapLoadErrorCode::spawn_not_walkable);
}

TEST_CASE("a map with no walkable cell has no valid default spawn") {
    // Every cell is a wall, so the deterministic policy finds nowhere to stand.
    const MapLoadError& error = expect_error(load_map(std::string_view{"3 1\n===\n"}));
    CHECK(error.code == MapLoadErrorCode::no_walkable_spawn);
}

TEST_CASE("the deterministic spawn policy breaks ties by row before column") {
    // Centre is (2,2). Every cell is a wall except two walkable cells that are
    // both at Chebyshev distance 2 from the centre: (2,0) on the top row and
    // (0,2) on the left column. The (distance, y, x) key must prefer the
    // smaller row, so the top-row cell (2,0) wins over the left-column (0,2).
    const std::string_view text = "5 5\n==.==\n=====\n.====\n=====\n=====\n";
    const Map& map = expect_map(load_map(text));
    CHECK(map.spawn() == Coordinates{2, 0});
    CHECK(is_walkable(map.terrain_at(map.spawn())));
}

TEST_CASE("a large fully-walled map rejects in bounded linear time") {
    // At the maximum dimension every cell is a wall. The default-spawn scan must
    // be a single O(width*height) pass: a cubic ring rescan would take many
    // billions of steps here and hang the suite, so merely completing with the
    // right code proves the linear bound without a flaky wall-clock assertion.
    constexpr std::size_t dimension = max_map_dimension;  // 1024
    std::string text = std::to_string(dimension) + " " + std::to_string(dimension) + "\n";
    const std::string wall_row(dimension, '=');
    text.reserve(text.size() + dimension * (dimension + 1));
    for (std::size_t row = 0; row < dimension; ++row) {
        text += wall_row;
        text.push_back('\n');
    }
    const MapLoadError& error = expect_error(load_map(std::string_view{text}));
    CHECK(error.code == MapLoadErrorCode::no_walkable_spawn);
}

TEST_CASE("an explicit spawn beyond int range is rejected instead of wrapping") {
    // 4294967298 == 2^32 + 2 truncates to 2 as a 32-bit int. Validating the raw
    // value against the map width must reject it as spawn_out_of_bounds rather
    // than silently wrapping into the valid column 2.
    const MapLoadError& error = expect_error(load_map(std::string_view{
        "NAM-MAP 1\nwidth 3\nheight 1\nspawn 4294967298 0\n---\n...\n"}));
    CHECK(error.code == MapLoadErrorCode::spawn_out_of_bounds);
    // The diagnostic points at the spawn line and reports the true large value.
    CHECK(error.line == 4);
    CHECK(error.message.find("4294967298") != std::string::npos);
}

TEST_CASE("opening a missing file yields file_open_error rather than crashing") {
    const MapLoadError& error = expect_error(load_map_file(fixture("no-such-file.map")));
    CHECK(error.code == MapLoadErrorCode::file_open_error);
}

TEST_CASE("error codes have stable identifier strings") {
    CHECK(to_string(MapLoadErrorCode::empty_input) == "empty_input");
    CHECK(to_string(MapLoadErrorCode::unknown_symbol) == "unknown_symbol");
    CHECK(to_string(MapLoadErrorCode::row_width_mismatch) == "row_width_mismatch");
    CHECK(to_string(MapLoadErrorCode::dimensions_out_of_range) == "dimensions_out_of_range");
    CHECK(to_string(MapLoadErrorCode::spawn_not_walkable) == "spawn_not_walkable");
    CHECK(to_string(MapLoadErrorCode::file_open_error) == "file_open_error");
}

}  // TEST_SUITE("parser")
