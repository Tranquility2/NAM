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
        cells[x] = Terrain::wall_horizontal;
        cells[(height - 1) * width + x] = Terrain::wall_horizontal;
    }
    for (std::size_t y = 1; y + 1 < height; ++y) {
        cells[y * width] = Terrain::wall_vertical;
        cells[y * width + width - 1] = Terrain::wall_vertical;
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
                        std::vector<Terrain>{Terrain::wall_horizontal, Terrain::wall_horizontal,
                                             Terrain::wall_horizontal, Terrain::wall_vertical,
                                             Terrain::open, Terrain::wall_vertical,
                                             Terrain::wall_horizontal, Terrain::wall_horizontal,
                                             Terrain::wall_horizontal},
                        Coordinates{1, 1}));

    CHECK(state.features().empty());
    CHECK(state.discovery_total() == 0u);
    CHECK(state.discoveries_found() == 0u);
    CHECK_FALSE(state.feature_at(Coordinates{1, 1}).has_value());
}

TEST_CASE("a hazard charges its penalty on top of the terrain cost without blocking the step") {
    GameState state(arena({LevelFeature{Coordinates{2, 1}, LevelFeatureKind::hazard}},
                          Coordinates{5, 3}));
    const std::uint32_t before = state.stamina();

    const MoveOutcome outcome = state.peek(Direction::right);
    CHECK(outcome.result == MoveResult::moved);
    REQUIRE(outcome.feature.has_value());
    CHECK(*outcome.feature == LevelFeatureKind::hazard);
    CHECK(outcome.stamina_cost == stamina_cost_of(Terrain::open).value() + hazard_stamina_penalty);

    const GameEvent event = state.move(Direction::right);
    CHECK(state.actor_position() == Coordinates{2, 1});
    CHECK(state.stamina() < before);
    CHECK(movement(event).outcome.stamina_after == state.stamina());
    CHECK_FALSE(movement(event).discovery_recorded);
}

TEST_CASE("a hazard the meter cannot pay for still succeeds and saturates at zero") {
    GameState state(arena({LevelFeature{Coordinates{2, 1}, LevelFeatureKind::hazard},
                           LevelFeature{Coordinates{3, 1}, LevelFeatureKind::hazard},
                           LevelFeature{Coordinates{4, 1}, LevelFeatureKind::hazard},
                           LevelFeature{Coordinates{5, 1}, LevelFeatureKind::hazard},
                           LevelFeature{Coordinates{5, 2}, LevelFeatureKind::hazard}},
                          Coordinates{1, 3}));

    for (int step = 0; step < 4; ++step) {
        const GameEvent event = state.move(Direction::right);
        CHECK(movement(event).outcome.result == MoveResult::moved);
    }
    CHECK(state.actor_position() == Coordinates{5, 1});
    // Four hazard crossings have driven the meter far below one hazard charge.
    REQUIRE(state.stamina() < hazard_stamina_penalty);

    // The fifth crossing costs more than the actor has. It is still legal: the
    // charge saturates at zero and open ground pays its passive recovery back.
    const std::uint32_t open_recovery = passive_recovery_of(Terrain::open).value();
    const GameEvent event = state.move(Direction::down);
    CHECK(movement(event).outcome.result == MoveResult::moved);
    CHECK(movement(event).outcome.stamina_cost ==
          stamina_cost_of(Terrain::open).value() + hazard_stamina_penalty);
    CHECK(state.actor_position() == Coordinates{5, 2});
    CHECK(state.stamina() == open_recovery);
}

TEST_CASE("a safe landmark restores the meter completely every time it is entered") {
    GameState state(arena({LevelFeature{Coordinates{2, 1}, LevelFeatureKind::hazard},
                           LevelFeature{Coordinates{3, 1}, LevelFeatureKind::safe_landmark}},
                          Coordinates{5, 3}));

    static_cast<void>(state.move(Direction::right));
    REQUIRE(state.stamina() < state.max_stamina());

    const GameEvent event = state.move(Direction::right);
    CHECK(state.stamina() == state.max_stamina());
    REQUIRE(movement(event).outcome.feature.has_value());
    CHECK(*movement(event).outcome.feature == LevelFeatureKind::safe_landmark);

    // Leaving and returning restores again: a safe landmark is stateless.
    static_cast<void>(state.move(Direction::down));
    static_cast<void>(state.move(Direction::down));
    REQUIRE(state.stamina() <= state.max_stamina());
    static_cast<void>(state.move(Direction::up));
    static_cast<void>(state.move(Direction::up));
    CHECK(state.stamina() == state.max_stamina());
}

TEST_CASE("a discovery is recorded once and changes no stamina") {
    GameState state(arena({LevelFeature{Coordinates{2, 1}, LevelFeatureKind::discovery}},
                          Coordinates{5, 3}));
    CHECK(state.discovery_total() == 1u);

    const MoveOutcome preview = state.peek(Direction::right);
    CHECK(preview.stamina_cost == stamina_cost_of(Terrain::open).value());

    const GameEvent first = state.move(Direction::right);
    CHECK(movement(first).discovery_recorded);
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
        const LevelTemplate level = template_of(tier);
        for (std::uint64_t index = 0; index < 48u; ++index) {
            const std::uint64_t seed = index * 0x9E3779B97F4A7C15ull + 0xFEEDull;
            const WorldGenerationResult result = generate_level(tier, seed);
            REQUIRE(std::holds_alternative<GeneratedWorld>(result));
            const GeneratedWorld& world = std::get<GeneratedWorld>(result);

            const std::vector<LevelFeature>& placed = world.map.layout().features;
            REQUIRE(placed.size() == level.content_slots.size());

            for (std::size_t slot = 0; slot < placed.size(); ++slot) {
                CHECK(placed[slot].kind == level.content_slots[slot].kind);
                CHECK(level.content_slots[slot].zone.contains(placed[slot].position));
                CHECK(stamina_cost_of(world.map.terrain_at(placed[slot].position)).has_value());
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
    CHECK(std::string(to_string(LevelFeatureKind::hazard)) == "hazard");
    CHECK(std::string(to_string(LevelFeatureKind::safe_landmark)) == "safe_landmark");
}

}  // TEST_SUITE("game")
