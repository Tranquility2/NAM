#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "coordinates.h"
#include "direction.h"
#include "game_event.h"
#include "game_state.h"
#include "map.h"
#include "map_parser.h"
#include "set_piece.h"
#include "terrain.h"
#include "world_generation.h"

namespace {

// A spread of seeds wide enough to meet a range of exit corners on every tier.
constexpr std::uint64_t kProbeSeeds = 24u;

[[nodiscard]] std::uint64_t probe_seed(std::uint64_t index) {
    return index * 0x9E3779B97F4A7C15ull + 0x5E7Dull;
}

constexpr LevelTier kAllTiers[] = {LevelTier::small, LevelTier::medium, LevelTier::large,
                                   LevelTier::x_large};

}  // namespace

TEST_SUITE("set_piece") {

TEST_CASE("the set-piece identifiers are stable and non-localized") {
    CHECK(std::string(to_string(SetPieceKind::ford)) == "ford");
    CHECK(std::string(to_string(SetPieceKind::ridge)) == "ridge");
    CHECK(std::string(to_string(SetPieceKind::lakeshore)) == "lakeshore");
    CHECK(std::string(to_string(SetPieceKind::high_pass)) == "high_pass");
}

TEST_CASE("every tier has its own set-piece and none of them can block the way") {
    // A set-piece is a crossing, not a wall. Made of walkable terrain it can only
    // ever add ways through a level, which is what lets generation lay one down
    // after a candidate has already been accepted as solvable.
    for (const LevelTier tier : kAllTiers) {
        CHECK(is_walkable(set_piece_terrain_of(set_piece_of(tier))));
        CHECK(set_piece_depth_of(set_piece_of(tier)) >= 1);
    }

    CHECK(set_piece_of(LevelTier::small) == SetPieceKind::ford);
    CHECK(set_piece_of(LevelTier::medium) == SetPieceKind::ridge);
    CHECK(set_piece_of(LevelTier::large) == SetPieceKind::lakeshore);
    CHECK(set_piece_of(LevelTier::x_large) == SetPieceKind::high_pass);
}

TEST_CASE("a stream and a lake are the same water told apart by how far you wade") {
    CHECK(set_piece_terrain_of(SetPieceKind::ford) ==
          set_piece_terrain_of(SetPieceKind::lakeshore));
    CHECK(set_piece_depth_of(SetPieceKind::ford) < set_piece_depth_of(SetPieceKind::lakeshore));
}

TEST_CASE("every generated level lays its tier's set-piece between spawn and the exit") {
    for (const LevelTier tier : kAllTiers) {
        for (std::uint64_t index = 0; index < kProbeSeeds; ++index) {
            const WorldGenerationResult result = generate_level(tier, probe_seed(index));
            REQUIRE(std::holds_alternative<GeneratedWorld>(result));
            const GeneratedWorld& world = std::get<GeneratedWorld>(result);
            const std::optional<SetPieceRegion>& band = world.map.layout().set_piece;
            REQUIRE(band.has_value());

            CHECK(band->kind == set_piece_of(tier));
            CHECK(band->max_x - band->min_x + 1 == set_piece_depth_of(band->kind));

            // Strictly between the two ends, so the crossing is a place on the way
            // rather than something underfoot at spawn or at the exit.
            const int low = world.map.spawn().x < world.exit_cell.x ? world.map.spawn().x
                                                                    : world.exit_cell.x;
            const int high = world.map.spawn().x < world.exit_cell.x ? world.exit_cell.x
                                                                     : world.map.spawn().x;
            CHECK(band->min_x > low);
            CHECK(band->max_x < high);
            CHECK(band->is_crossing(world.map.spawn()) == false);
            CHECK(band->is_crossing(world.exit_cell) == false);
        }
    }
}

TEST_CASE("the cells a set-piece covers are all its own walkable ground") {
    for (const LevelTier tier : kAllTiers) {
        for (std::uint64_t index = 0; index < kProbeSeeds; ++index) {
            const WorldGenerationResult result = generate_level(tier, probe_seed(index));
            REQUIRE(std::holds_alternative<GeneratedWorld>(result));
            const GeneratedWorld& world = std::get<GeneratedWorld>(result);
            const SetPieceRegion& band = *world.map.layout().set_piece;
            const Terrain ground = set_piece_terrain_of(band.kind);

            for (int y = band.min_y; y <= band.max_y; ++y) {
                for (int x = band.min_x; x <= band.max_x; ++x) {
                    const Terrain here = world.map.terrain_at(Coordinates{x, y});
                    // Where the carved route crosses, the band keeps the route's
                    // open ground: that gap is the ford, the gap in the pass.
                    CHECK((here == ground || here == Terrain::open));
                    CHECK(is_walkable(here));
                }
            }
        }
    }
}

TEST_CASE("no route to the exit can avoid the crossing") {
    // The band spans a range of columns between spawn and the exit, and one step
    // changes x by at most one, so any walk that reaches the exit stands in those
    // columns on the way. This is checked here against the real shortest walk
    // rather than argued: the level does not have to hope the player finds it.
    for (const LevelTier tier : kAllTiers) {
        for (std::uint64_t index = 0; index < kProbeSeeds; ++index) {
            const WorldGenerationResult result = generate_level(tier, probe_seed(index));
            REQUIRE(std::holds_alternative<GeneratedWorld>(result));
            const GeneratedWorld& world = std::get<GeneratedWorld>(result);
            const SetPieceRegion& band = *world.map.layout().set_piece;

            const std::vector<Coordinates> walk =
                shortest_path(world.map, world.map.spawn(), world.exit_cell);
            REQUIRE(walk.empty() == false);

            bool crossed = false;
            for (const Coordinates step : walk) {
                if (band.is_crossing(step)) crossed = true;
            }
            CHECK(crossed);
        }
    }
}

TEST_CASE("crossing the set-piece is reported once and is then inert") {
    const WorldGenerationResult result = generate_level(LevelTier::small, probe_seed(3));
    REQUIRE(std::holds_alternative<GeneratedWorld>(result));
    const GeneratedWorld& world = std::get<GeneratedWorld>(result);
    const SetPieceRegion band = *world.map.layout().set_piece;

    GameState state(Map(world.map));
    REQUIRE(state.set_piece().has_value());
    CHECK(state.set_piece_crossed() == false);

    // Walk toward the exit and count how many moves reported the crossing.
    const int toward = world.exit_cell.x >= world.map.spawn().x ? 1 : -1;
    const Direction along = toward > 0 ? Direction::right : Direction::left;

    std::uint32_t reports = 0;
    bool stood_inside = false;
    for (int step = 0; step < 64; ++step) {
        const GameEvent event = state.move(along);
        const MoveAttemptedEvent& move = std::get<MoveAttemptedEvent>(event.data);
        if (move.set_piece_crossed) {
            ++reports;
            CHECK(*move.set_piece_crossed == band.kind);
            CHECK(band.is_crossing(state.actor_position()));
        }
        if (band.is_crossing(state.actor_position())) stood_inside = true;
        if (move.outcome.result != MoveResult::moved) break;
    }

    REQUIRE(stood_inside);
    CHECK(reports == 1u);
    CHECK(state.set_piece_crossed());
}

TEST_CASE("a handcrafted map has no crossing to meet") {
    const MapLoadResult loaded =
        load_map("NAM-MAP 1\nwidth 3\nheight 3\nspawn 0 0\n---\n...\n...\n...\n");
    REQUIRE(std::holds_alternative<Map>(loaded));

    GameState state(std::get<Map>(loaded));
    CHECK(state.set_piece().has_value() == false);

    const GameEvent event = state.move(Direction::right);
    const MoveAttemptedEvent& move = std::get<MoveAttemptedEvent>(event.data);
    CHECK(move.set_piece_crossed.has_value() == false);
    CHECK(state.set_piece_crossed() == false);
}

}  // TEST_SUITE("set_piece")
