#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "coordinates.h"
#include "direction.h"
#include "game_event.h"
#include "game_state.h"
#include "map.h"
#include "map_parser.h"
#include "move_outcome.h"
#include "terrain.h"
#include "visibility.h"

namespace {

Map make_map(std::string_view text) {
    MapLoadResult result = load_map(text);
    REQUIRE(std::holds_alternative<Map>(result));
    return std::get<Map>(std::move(result));
}

MoveOutcome outcome_of(const GameEvent& event) {
    return std::get<MoveAttemptedEvent>(event.data).outcome;
}

RestedEvent rested_of(const GameEvent& event) {
    return std::get<RestedEvent>(event.data);
}

// A 2x1 map: an open spawn at (0,0) and a single destination cell to its right
// whose terrain is chosen by `symbol`, so a right move enters exactly that
// terrain from full stamina.
Map spawn_then(char symbol) {
    std::string text = "NAM-MAP 1\nwidth 2\nheight 1\nspawn 0 0\n---\n.";
    text.push_back(symbol);
    text.push_back('\n');
    return make_map(text);
}

// A flat, one-character-per-cell signature of every visibility cell, so a test
// can assert exploration memory is byte-for-byte unchanged across an attempt.
std::string visibility_signature(const GameState& state) {
    const VisibilityMap& visibility = state.visibility();
    std::string signature;
    for (int y = 0; y < static_cast<int>(visibility.height()); ++y) {
        for (int x = 0; x < static_cast<int>(visibility.width()); ++x) {
            signature.push_back(static_cast<char>('0' +
                static_cast<int>(visibility.at(Coordinates{x, y}))));
        }
    }
    return signature;
}

}  // namespace

TEST_SUITE("game") {

TEST_CASE("every terrain has its exact locked stamina cost") {
    CHECK(stamina_cost_of(Terrain::open).has_value());
    CHECK(stamina_cost_of(Terrain::open).value() == 1u);
    CHECK(stamina_cost_of(Terrain::fields).has_value());
    CHECK(stamina_cost_of(Terrain::fields).value() == 2u);
    CHECK(stamina_cost_of(Terrain::hill).has_value());
    CHECK(stamina_cost_of(Terrain::hill).value() == 2u);
    CHECK(stamina_cost_of(Terrain::water).has_value());
    CHECK(stamina_cost_of(Terrain::water).value() == 3u);
    CHECK(stamina_cost_of(Terrain::mountain).has_value());
    CHECK(stamina_cost_of(Terrain::mountain).value() == 4u);
    CHECK_FALSE(stamina_cost_of(Terrain::wall_horizontal).has_value());
    CHECK_FALSE(stamina_cost_of(Terrain::wall_vertical).has_value());
}

TEST_CASE("walkability is derived from the stamina cost table") {
    CHECK(is_walkable(Terrain::open));
    CHECK(is_walkable(Terrain::fields));
    CHECK(is_walkable(Terrain::hill));
    CHECK(is_walkable(Terrain::water));
    CHECK(is_walkable(Terrain::mountain));
    CHECK_FALSE(is_walkable(Terrain::wall_horizontal));
    CHECK_FALSE(is_walkable(Terrain::wall_vertical));
}

TEST_CASE("a new game starts at full stamina and never charges the spawn") {
    GameState state(spawn_then('.'));
    CHECK(state.stamina() == 20u);
    CHECK(state.max_stamina() == 20u);
    CHECK(GameState::maximum_stamina == 20u);
    // Spawn terrain (open) is never charged: full stamina remains before any move.
    CHECK(state.actor_terrain() == Terrain::open);
}

TEST_CASE("successful movement onto each terrain spends its exact cost") {
    struct Case {
        char symbol;
        Terrain terrain;
        std::uint32_t cost;
    };
    const Case cases[] = {
        {'.', Terrain::open, 1u},
        {'x', Terrain::fields, 2u},
        {'^', Terrain::hill, 2u},
        {'~', Terrain::water, 3u},
        {'@', Terrain::mountain, 4u},
    };

    for (const Case& c : cases) {
        GameState state(spawn_then(c.symbol));
        const MoveOutcome outcome = outcome_of(state.move(Direction::right));
        CHECK(outcome.result == MoveResult::moved);
        CHECK(outcome.terrain == c.terrain);
        CHECK(outcome.stamina_cost == c.cost);
        CHECK(outcome.stamina_before == 20u);
        CHECK(outcome.stamina_after == 20u - c.cost);
        CHECK(outcome.to == Coordinates{1, 0});
        CHECK(state.actor_position() == Coordinates{1, 0});
        CHECK(state.stamina() == 20u - c.cost);
    }
}

TEST_CASE("a mixed path can leave two stamina and then block a four-cost mountain") {
    GameState state(make_map("NAM-MAP 1\nwidth 8\nheight 1\nspawn 0 0\n---\n.~@~@.~@\n"));

    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // water, 20->17
    CHECK(state.stamina() == 17u);
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // mountain, 17->13
    CHECK(state.stamina() == 13u);
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // water, 13->10
    CHECK(state.stamina() == 10u);
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // mountain, 10->6
    CHECK(state.stamina() == 6u);
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // open, 6->5
    CHECK(state.stamina() == 5u);
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // water, 5->2
    CHECK(state.stamina() == 2u);

    const Coordinates before_pos = state.actor_position();
    const std::string before_render = state.render();
    const std::string before_fog = visibility_signature(state);

    const MoveOutcome blocked = outcome_of(state.move(Direction::right));  // mountain, need 4
    CHECK(blocked.result == MoveResult::blocked_by_stamina);
    CHECK(blocked.terrain == Terrain::mountain);
    CHECK(blocked.stamina_cost == 4u);
    CHECK(blocked.stamina_before == 2u);
    CHECK(blocked.stamina_after == 2u);
    CHECK(blocked.from == before_pos);
    CHECK(blocked.to == before_pos);

    // The block preserves actor position, stamina, visibility, and serialization.
    CHECK(state.actor_position() == before_pos);
    CHECK(state.stamina() == 2u);
    CHECK(state.render() == before_render);
    CHECK(visibility_signature(state) == before_fog);
}

TEST_CASE("an exact-cost path reaches zero and then blocks without underflow") {
    GameState state(make_map("NAM-MAP 1\nwidth 7\nheight 1\nspawn 0 0\n---\n.@@@@@.\n"));

    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 20->16
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 16->12
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 12->8
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 8->4
    const MoveOutcome last = outcome_of(state.move(Direction::right));            // 4->0
    CHECK(last.result == MoveResult::moved);
    CHECK(last.stamina_after == 0u);
    CHECK(state.stamina() == 0u);

    const MoveOutcome blocked = outcome_of(state.move(Direction::right));  // open, need 1
    CHECK(blocked.result == MoveResult::blocked_by_stamina);
    CHECK(blocked.terrain == Terrain::open);
    CHECK(blocked.stamina_cost == 1u);
    CHECK(blocked.stamina_before == 0u);
    CHECK(blocked.stamina_after == 0u);
    CHECK(state.stamina() == 0u);
    CHECK(state.actor_position() == Coordinates{5, 0});
}

TEST_CASE("boundary and wall blocks cost zero at non-full stamina") {
    GameState state(make_map("NAM-MAP 1\nwidth 3\nheight 1\nspawn 0 0\n---\n.x=\n"));

    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // fields, 20->18
    CHECK(state.stamina() == 18u);

    const MoveOutcome wall = outcome_of(state.move(Direction::right));  // wall
    CHECK(wall.result == MoveResult::blocked_by_terrain);
    CHECK(wall.stamina_cost == 0u);
    CHECK(wall.stamina_before == 18u);
    CHECK(wall.stamina_after == 18u);

    const MoveOutcome edge = outcome_of(state.move(Direction::up));  // off the top edge
    CHECK(edge.result == MoveResult::blocked_by_boundary);
    CHECK(edge.stamina_cost == 0u);
    CHECK(edge.stamina_before == 18u);
    CHECK(edge.stamina_after == 18u);

    CHECK(state.stamina() == 18u);
    CHECK(state.actor_position() == Coordinates{1, 0});
}

TEST_CASE("repeated peek predicts stamina without mutating any state") {
    GameState state(make_map("NAM-MAP 1\nwidth 2\nheight 1\nspawn 0 0\n---\n.~\n"));
    const std::string before_fog = visibility_signature(state);

    const MoveOutcome first = state.peek(Direction::right);
    const MoveOutcome second = state.peek(Direction::right);
    CHECK(first.result == MoveResult::moved);
    CHECK(first.result == second.result);
    CHECK(first.stamina_cost == second.stamina_cost);
    CHECK(first.stamina_before == second.stamina_before);
    CHECK(first.stamina_after == second.stamina_after);
    CHECK(first.stamina_cost == 3u);
    CHECK(first.stamina_before == 20u);
    CHECK(first.stamina_after == 17u);

    // No peek mutated actor, stamina, or visibility.
    CHECK(state.actor_position() == Coordinates{0, 0});
    CHECK(state.stamina() == 20u);
    CHECK(visibility_signature(state) == before_fog);

    // The first emitted event still starts the sequence at zero.
    const GameEvent event = state.move(Direction::right);
    CHECK(event.sequence == 0);
}

TEST_CASE("resting at full stamina is a heroic no-op and still emits one event") {
    GameState state(spawn_then('.'));
    const std::uint32_t provisions_before = state.provisions();
    CHECK(state.stamina() == 20u);

    const GameEvent event = state.rest();
    const RestedEvent rested = rested_of(event);
    CHECK(rested.result == RestResult::already_full);
    CHECK(rested.terrain == Terrain::open);
    CHECK(rested.stamina_before == 20u);
    CHECK(rested.stamina_recovered == 0u);
    CHECK(rested.stamina_after == 20u);
    CHECK(rested.provisions_before == provisions_before);
    CHECK(rested.provisions_after == provisions_before);
    CHECK(state.stamina() == 20u);
    CHECK(state.provisions() == provisions_before);
    CHECK(event.sequence == 0u);
}

TEST_CASE("resting below full on mountain recovers two and spends one provision") {
    GameState state(make_map("NAM-MAP 1\nwidth 3\nheight 1\nspawn 0 0\n---\n.@.\n"));
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 20->16
    REQUIRE(state.actor_terrain() == Terrain::mountain);
    const std::uint32_t provisions_before = state.provisions();

    const GameEvent event = state.rest();
    const RestedEvent rested = rested_of(event);
    CHECK(rested.result == RestResult::recovered);
    CHECK(rested.terrain == Terrain::mountain);
    CHECK(rested.stamina_before == 16u);
    CHECK(rested.stamina_recovered == 2u);
    CHECK(rested.stamina_after == 18u);
    CHECK(rested.provisions_before == provisions_before);
    CHECK(rested.provisions_after + 1u == provisions_before);
    CHECK(state.stamina() == 18u);
    CHECK(state.provisions() == provisions_before - 1u);
    CHECK(event.sequence == 1u);
}

TEST_CASE("resting on fields is terrain based and capped at twenty") {
    GameState state(make_map("NAM-MAP 1\nwidth 3\nheight 1\nspawn 0 0\n---\n.x.\n"));
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 20->18
    REQUIRE(state.actor_terrain() == Terrain::fields);
    const std::uint32_t provisions_before = state.provisions();

    const RestedEvent rested = rested_of(state.rest());
    CHECK(rested.result == RestResult::recovered);
    CHECK(rested.terrain == Terrain::fields);
    CHECK(rested.stamina_before == 18u);
    CHECK(rested.stamina_recovered == 2u);  // fields recover 6, but cap is 20.
    CHECK(rested.stamina_after == 20u);
    CHECK(rested.provisions_before == provisions_before);
    CHECK(rested.provisions_after + 1u == provisions_before);
    CHECK(state.stamina() == 20u);
    CHECK(state.stamina() <= GameState::maximum_stamina);
}

TEST_CASE("resting from seventeen eighteen and nineteen only tops up to the maximum") {
    struct Case {
        std::uint32_t before;
        std::uint32_t recovered;
    };
    const Case cases[] = {{17u, 3u}, {18u, 2u}, {19u, 1u}};
    for (const Case& c : cases) {
        GameState state(
            make_map("NAM-MAP 1\nwidth 25\nheight 1\nspawn 0 0\n---\n.........................\n"));
        const std::uint32_t moves_needed = GameState::maximum_stamina - c.before;
        for (std::uint32_t i = 0; i < moves_needed; ++i) {
            CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);
        }
        REQUIRE(state.stamina() == c.before);
        const RestedEvent rested = rested_of(state.rest());
        CHECK(rested.result == RestResult::recovered);
        CHECK(rested.stamina_before == c.before);
        CHECK(rested.stamina_recovered == c.recovered);
        CHECK(rested.stamina_after == 20u);
        CHECK(state.stamina() == 20u);
    }
}

TEST_CASE("resting below full with zero provisions is a no-op") {
    GameState state(make_map("NAM-MAP 1\nwidth 3\nheight 1\nspawn 0 0\n---\n.@.\n"));
    REQUIRE(state.provisions() >= 1u);

    std::uint32_t safety = 0;
    while (state.provisions() > 0u) {
        const Direction direction =
            state.actor_position().x == 0 ? Direction::right : Direction::left;
        const GameEvent moved = state.move(direction);
        REQUIRE(outcome_of(moved).result == MoveResult::moved);
        REQUIRE(state.stamina() < GameState::maximum_stamina);

        const std::uint32_t provisions_before = state.provisions();
        const RestedEvent rested = rested_of(state.rest());
        CHECK(rested.result == RestResult::recovered);
        CHECK(rested.provisions_before == provisions_before);
        CHECK(rested.provisions_after + 1u == provisions_before);

        ++safety;
        REQUIRE(safety < 100u);
    }

    const Direction direction = state.actor_position().x == 0 ? Direction::right : Direction::left;
    const GameEvent moved = state.move(direction);
    REQUIRE(outcome_of(moved).result == MoveResult::moved);
    const std::uint32_t stamina_before = state.stamina();
    REQUIRE(stamina_before < GameState::maximum_stamina);

    const GameEvent event = state.rest();
    const RestedEvent rested = rested_of(event);
    CHECK(rested.result == RestResult::no_provisions);
    CHECK(rested.stamina_before == stamina_before);
    CHECK(rested.stamina_recovered == 0u);
    CHECK(rested.stamina_after == stamina_before);
    CHECK(rested.provisions_before == 0u);
    CHECK(rested.provisions_after == 0u);
    CHECK(state.stamina() == stamina_before);
    CHECK(state.provisions() == 0u);
}

TEST_CASE("resting on fields from zero enables the next water move") {
    // Drain to zero on fields (2-cost terrain), then recover 6 on fields and take
    // a water move that was previously unaffordable.
    GameState state(make_map("NAM-MAP 1\nwidth 8\nheight 1\nspawn 0 0\n---\n.@@@@xx~\n"));
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 20->16
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 16->12
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 12->8
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 8->4
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 4->2
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 2->0
    CHECK(state.actor_terrain() == Terrain::fields);
    CHECK(state.stamina() == 0u);

    CHECK(state.peek(Direction::right).result == MoveResult::blocked_by_stamina);

    const std::uint32_t provisions_before = state.provisions();
    const RestedEvent rested = rested_of(state.rest());
    CHECK(rested.result == RestResult::recovered);
    CHECK(rested.terrain == Terrain::fields);
    CHECK(rested.stamina_after == 6u);
    CHECK(rested.provisions_before == provisions_before);
    CHECK(rested.provisions_after + 1u == provisions_before);

    const MoveOutcome water = outcome_of(state.move(Direction::right));  // water, cost 3.
    CHECK(water.result == MoveResult::moved);
    CHECK(water.terrain == Terrain::water);
    CHECK(water.stamina_cost == 3u);
    CHECK(state.stamina() == 3u);
}

TEST_CASE("resting preserves actor position map serialization and visibility") {
    GameState state(make_map("NAM-MAP 1\nwidth 3\nheight 1\nspawn 0 0\n---\n.@.\n"));
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 20->16

    const Coordinates before_pos = state.actor_position();
    const std::string before_render = state.render();
    const std::string before_fog = visibility_signature(state);

    const GameEvent event = state.rest();
    CHECK(std::holds_alternative<RestedEvent>(event.data));

    CHECK(state.actor_position() == before_pos);
    CHECK(state.render() == before_render);
    CHECK(visibility_signature(state) == before_fog);
}

}  // TEST_SUITE("game")
