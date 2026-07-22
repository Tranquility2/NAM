#include <doctest/doctest.h>

#include <array>
#include <stdexcept>
#include <vector>

#include "coordinates.h"
#include "direction.h"
#include "game_state.h"
#include "map.h"
#include "terrain.h"

TEST_SUITE("parser") {

TEST_CASE("every terrain symbol round-trips through the glyph table") {
    constexpr std::array<Terrain, 7> all{
        Terrain::open,           Terrain::mountain,      Terrain::water, Terrain::fields,
        Terrain::hill,           Terrain::wall_horizontal, Terrain::wall_vertical};
    for (const Terrain terrain : all) {
        const auto decoded = terrain_from_symbol(symbol_of(terrain));
        REQUIRE(decoded.has_value());
        CHECK(*decoded == terrain);
    }
}

TEST_CASE("walls are the only impassable terrain") {
    CHECK(is_walkable(Terrain::open));
    CHECK(is_walkable(Terrain::mountain));
    CHECK(is_walkable(Terrain::water));
    CHECK(is_walkable(Terrain::fields));
    CHECK(is_walkable(Terrain::hill));
    CHECK_FALSE(is_walkable(Terrain::wall_horizontal));
    CHECK_FALSE(is_walkable(Terrain::wall_vertical));
}

TEST_CASE("terrain visibility radius maps to the canonical 2/2/2/3/4/0/0 table") {
    // TASK-005 / TEST-001: base terrain sees radius 2, hills 3, mountains 4, and
    // both unoccupiable wall variants document radius 0.
    CHECK(visibility_radius_of(Terrain::open) == 2);
    CHECK(visibility_radius_of(Terrain::fields) == 2);
    CHECK(visibility_radius_of(Terrain::water) == 2);
    CHECK(visibility_radius_of(Terrain::hill) == 3);
    CHECK(visibility_radius_of(Terrain::mountain) == 4);
    CHECK(visibility_radius_of(Terrain::wall_horizontal) == 0);
    CHECK(visibility_radius_of(Terrain::wall_vertical) == 0);
}

TEST_CASE("public GameState radius constants derive from the terrain table") {
    // TASK-005 / TEST-002: the exposed constants must equal the canonical table
    // rather than hard-coded literals so the two can never drift.
    CHECK(GameState::base_visibility_radius == visibility_radius_of(Terrain::open));
    CHECK(GameState::hill_visibility_radius == visibility_radius_of(Terrain::hill));
    CHECK(GameState::mountain_visibility_radius == visibility_radius_of(Terrain::mountain));
    CHECK(GameState::base_visibility_radius == 2);
    CHECK(GameState::hill_visibility_radius == 3);
    CHECK(GameState::mountain_visibility_radius == 4);
}

TEST_CASE("unknown symbols decode to no terrain") {
    CHECK_FALSE(terrain_from_symbol('Z').has_value());
    CHECK_FALSE(terrain_from_symbol(' ').has_value());
    CHECK_FALSE(terrain_from_symbol('\n').has_value());
    CHECK_FALSE(terrain_from_symbol('\0').has_value());
}

TEST_CASE("coordinate arithmetic and comparison behave as value types") {
    Coordinates a{2, 3};
    const Coordinates b{-1, 4};
    CHECK(a + b == Coordinates{1, 7});
    a += b;
    CHECK(a == Coordinates{1, 7});
    CHECK(a != b);
    CHECK(Coordinates{0, 0} == Coordinates{});
}

TEST_CASE("direction deltas match a downward-growing y axis") {
    CHECK(direction_delta(Direction::up) == Coordinates{0, -1});
    CHECK(direction_delta(Direction::down) == Coordinates{0, 1});
    CHECK(direction_delta(Direction::left) == Coordinates{-1, 0});
    CHECK(direction_delta(Direction::right) == Coordinates{1, 0});
}

TEST_CASE("Map::contains guards every edge before indexing") {
    // A 3x2 all-open map so terrain_at is always safe where contains is true.
    Map map(3, 2, std::vector<Terrain>(6, Terrain::open), Coordinates{0, 0});

    CHECK(map.width() == 3);
    CHECK(map.height() == 2);

    // Inside: all four corners.
    CHECK(map.contains({0, 0}));
    CHECK(map.contains({2, 0}));
    CHECK(map.contains({0, 1}));
    CHECK(map.contains({2, 1}));

    // Outside: negatives and past-the-edge on each axis.
    CHECK_FALSE(map.contains({-1, 0}));
    CHECK_FALSE(map.contains({0, -1}));
    CHECK_FALSE(map.contains({3, 0}));
    CHECK_FALSE(map.contains({0, 2}));

    // terrain_at is valid for every contained coordinate.
    CHECK(map.terrain_at({2, 1}) == Terrain::open);
}

TEST_CASE("Map rejects malformed geometry at construction") {
    // Zero dimension.
    CHECK_THROWS_AS(Map(0, 2, std::vector<Terrain>{}, Coordinates{0, 0}), std::invalid_argument);
    // Cell count mismatch.
    CHECK_THROWS_AS(Map(2, 2, std::vector<Terrain>(3, Terrain::open), Coordinates{0, 0}),
                    std::invalid_argument);
    // Spawn outside the bounds.
    CHECK_THROWS_AS(Map(2, 2, std::vector<Terrain>(4, Terrain::open), Coordinates{2, 0}),
                    std::invalid_argument);
}

}  // TEST_SUITE("parser")
