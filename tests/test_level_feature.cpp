#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "game_state.h"
#include "level_feature.h"
#include "level_template.h"
#include "map.h"
#include "visibility.h"
#include "terrain.h"
#include "world_generation.h"

namespace {

// A tiny open arena with authored content placed by hand. Using a handcrafted
// layout rather than a generated one keeps every feature rule test independent of
// the generator's placement choices.
Map arena(std::vector<LevelFeature> features, Coordinates exit_cell) {
    constexpr std::size_t width = 7;
    constexpr std::size_t height = 5;
    std::vector<Terrain> cells(width * height, Terrain::open);
    for (std::size_t x = 0; x < width; ++x) {
        cells[x] = Terrain::cliff;
        cells[(height - 1) * width + x] = Terrain::cliff;
    }
    for (std::size_t y = 1; y + 1 < height; ++y) {
        cells[y * width] = Terrain::cliff;
        cells[y * width + width - 1] = Terrain::cliff;
    }

    LevelLayout layout;
    layout.exit = exit_cell;
    layout.features = std::move(features);
    return Map(width, height, std::move(cells), Coordinates{1, 1}, std::move(layout));
}

const MoveAttemptedEvent& movement(const GameEvent& event) {
    REQUIRE(std::holds_alternative<MoveAttemptedEvent>(event.data));
    return std::get<MoveAttemptedEvent>(event.data);
}

}  // namespace

TEST_SUITE("game") {

TEST_CASE("a handcrafted map carries no authored content") {
    GameState state(Map(3, 3,
                        std::vector<Terrain>{Terrain::cliff, Terrain::cliff,
                                             Terrain::cliff, Terrain::cliff,
                                             Terrain::open, Terrain::cliff,
                                             Terrain::cliff, Terrain::cliff,
                                             Terrain::cliff},
                        Coordinates{1, 1}));

    CHECK(state.features().empty());
    CHECK(state.discovery_total() == 0u);
    CHECK(state.discoveries_found() == 0u);
    CHECK_FALSE(state.feature_at(Coordinates{1, 1}).has_value());
}

TEST_CASE("a vantage point grants one wide reveal on first entry and is then inert") {
    GameState state(arena({LevelFeature{Coordinates{2, 1}, LevelFeatureKind::vantage_point}},
                          Coordinates{5, 3}));

    // peek stays a pure preview: it reports the feature but resolves nothing.
    const MoveOutcome preview = state.peek(Direction::right);
    CHECK(preview.result == MoveResult::moved);
    REQUIRE(preview.feature.has_value());
    CHECK(*preview.feature == LevelFeatureKind::vantage_point);

    const GameEvent first = state.move(Direction::right);
    CHECK(movement(first).wide_reveal_granted);
    CHECK_FALSE(movement(first).discovery_recorded);

    // Leaving and returning does not fire it again.
    static_cast<void>(state.move(Direction::down));
    const GameEvent again = state.move(Direction::up);
    CHECK(movement(again).outcome.result == MoveResult::moved);
    CHECK_FALSE(movement(again).wide_reveal_granted);
}

TEST_CASE("a vantage point is an ordinary walkable cell and never blocks") {
    GameState state(arena({LevelFeature{Coordinates{2, 1}, LevelFeatureKind::vantage_point}},
                          Coordinates{5, 3}));

    const GameEvent event = state.move(Direction::right);
    CHECK(movement(event).outcome.result == MoveResult::moved);
    CHECK(movement(event).outcome.terrain == Terrain::open);
    CHECK(state.actor_position() == Coordinates{2, 1});
}

TEST_CASE("a vantage point reveals ground no terrain radius could reach") {
    // A wide arena so the reveal is not clipped away by the map edges. The vantage
    // sits far enough from the spawn that the cell checked below is outside the
    // spawn's own sight square but inside the vantage's.
    constexpr std::size_t width = 31;
    constexpr std::size_t height = 25;
    std::vector<Terrain> cells(width * height, Terrain::open);
    for (std::size_t x = 0; x < width; ++x) {
        cells[x] = Terrain::cliff;
        cells[(height - 1) * width + x] = Terrain::cliff;
    }
    for (std::size_t y = 1; y + 1 < height; ++y) {
        cells[y * width] = Terrain::cliff;
        cells[y * width + width - 1] = Terrain::cliff;
    }
    const Coordinates spawn{15, 12};
    LevelLayout layout;
    layout.exit = Coordinates{1, 1};
    layout.features = {LevelFeature{Coordinates{16, 12}, LevelFeatureKind::vantage_point}};
    GameState state(Map(width, height, std::move(cells), spawn, std::move(layout)));

    // Open ground sees 3, so a cell 8 away is still unexplored before the vantage.
    const Coordinates distant{16 + 8, 12};
    REQUIRE(state.visibility().at(distant) == CellVisibility::unexplored);

    const GameEvent event = state.move(Direction::right);
    REQUIRE(movement(event).wide_reveal_granted);
    CHECK(state.visibility().at(distant) == CellVisibility::visible);
}

TEST_CASE("a discovery is recorded once and grants no wide reveal") {
    GameState state(arena({LevelFeature{Coordinates{2, 1}, LevelFeatureKind::discovery}},
                          Coordinates{5, 3}));
    CHECK(state.discovery_total() == 1u);

    const MoveOutcome preview = state.peek(Direction::right);
    CHECK(preview.result == MoveResult::moved);
    CHECK(preview.feature == LevelFeatureKind::discovery);

    const GameEvent first = state.move(Direction::right);
    CHECK(movement(first).discovery_recorded);
    CHECK_FALSE(movement(first).wide_reveal_granted);
    CHECK(state.discoveries_found() == 1u);

    // Re-entering the same cell is an ordinary step.
    static_cast<void>(state.move(Direction::down));
    const GameEvent again = state.move(Direction::up);
    CHECK_FALSE(movement(again).discovery_recorded);
    CHECK(state.discoveries_found() == 1u);
}

TEST_CASE("a blocked attempt reports no feature and resolves nothing") {
    GameState state(arena({LevelFeature{Coordinates{2, 1}, LevelFeatureKind::discovery}},
                          Coordinates{5, 3}));

    const GameEvent event = state.move(Direction::up);
    CHECK(movement(event).outcome.result == MoveResult::blocked_by_terrain);
    CHECK_FALSE(movement(event).outcome.feature.has_value());
    CHECK_FALSE(movement(event).discovery_recorded);
    CHECK(state.discoveries_found() == 0u);
}

TEST_CASE("every generated level places its whole content budget on reachable walkable cells") {
    for (const LevelTier tier : {LevelTier::small, LevelTier::medium}) {
        for (std::uint64_t index = 0; index < 48u; ++index) {
            const std::uint64_t seed = index * 0x9E3779B97F4A7C15ull + 0xFEEDull;
            const WorldGenerationResult result = generate_level(tier, seed);
            REQUIRE(std::holds_alternative<GeneratedWorld>(result));
            const GeneratedWorld& world = std::get<GeneratedWorld>(result);
            const LevelTemplate level = template_of(tier, world.exit_corner);

            const std::vector<LevelFeature>& placed = world.map.layout().features;
            REQUIRE(placed.size() == level.content_slots.size());

            for (std::size_t slot = 0; slot < placed.size(); ++slot) {
                CHECK(placed[slot].kind == level.content_slots[slot].kind);
                CHECK(level.content_slots[slot].zone.contains(placed[slot].position));
                CHECK(is_walkable(world.map.terrain_at(placed[slot].position)));
                CHECK(placed[slot].position != world.map.spawn());
                CHECK(placed[slot].position != world.exit_cell);
            }

            // No two features share a cell.
            for (std::size_t left = 0; left + 1 < placed.size(); ++left) {
                for (std::size_t right = left + 1; right < placed.size(); ++right) {
                    CHECK(placed[left].position != placed[right].position);
                }
            }
        }
    }
}

TEST_CASE("content placement is deterministic for one tier and seed") {
    const WorldGenerationResult first = generate_level(LevelTier::medium, 0xC0FFEEull);
    const WorldGenerationResult second = generate_level(LevelTier::medium, 0xC0FFEEull);
    REQUIRE(std::holds_alternative<GeneratedWorld>(first));
    REQUIRE(std::holds_alternative<GeneratedWorld>(second));

    const std::vector<LevelFeature>& left = std::get<GeneratedWorld>(first).map.layout().features;
    const std::vector<LevelFeature>& right = std::get<GeneratedWorld>(second).map.layout().features;
    REQUIRE(left.size() == right.size());
    for (std::size_t index = 0; index < left.size(); ++index) {
        CHECK(left[index].position == right[index].position);
        CHECK(left[index].kind == right[index].kind);
    }
}

TEST_CASE("the feature kind identifiers are stable and non-localized") {
    CHECK(std::string(to_string(LevelFeatureKind::discovery)) == "discovery");
    CHECK(std::string(to_string(LevelFeatureKind::vantage_point)) == "vantage_point");
}

}  // TEST_SUITE("game")
