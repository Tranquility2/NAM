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
    CHECK(state.stamina() == 12u);
    CHECK(state.max_stamina() == 12u);
    CHECK(GameState::maximum_stamina == 12u);
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
        CHECK(outcome.stamina_before == 12u);
        CHECK(outcome.stamina_after == 12u - c.cost);
        CHECK(outcome.to == Coordinates{1, 0});
        CHECK(state.actor_position() == Coordinates{1, 0});
        CHECK(state.stamina() == 12u - c.cost);
    }
}

TEST_CASE("a mixed path can leave two stamina and then block a four-cost mountain") {
    GameState state(make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.~@~@\n"));

    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // water, 12->9
    CHECK(state.stamina() == 9u);
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // mountain, 9->5
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
    GameState state(make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.@@@.\n"));

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
    CHECK(state.actor_position() == Coordinates{3, 0});
}

TEST_CASE("boundary and wall blocks cost zero at non-full stamina") {
    GameState state(make_map("NAM-MAP 1\nwidth 3\nheight 1\nspawn 0 0\n---\n.x=\n"));

    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // fields, 12->10
    CHECK(state.stamina() == 10u);

    const MoveOutcome wall = outcome_of(state.move(Direction::right));  // wall
    CHECK(wall.result == MoveResult::blocked_by_terrain);
    CHECK(wall.stamina_cost == 0u);
    CHECK(wall.stamina_before == 10u);
    CHECK(wall.stamina_after == 10u);

    const MoveOutcome edge = outcome_of(state.move(Direction::up));  // off the top edge
    CHECK(edge.result == MoveResult::blocked_by_boundary);
    CHECK(edge.stamina_cost == 0u);
    CHECK(edge.stamina_before == 10u);
    CHECK(edge.stamina_after == 10u);

    CHECK(state.stamina() == 10u);
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
    CHECK(first.stamina_before == 12u);
    CHECK(first.stamina_after == 9u);

    // No peek mutated actor, stamina, or visibility.
    CHECK(state.actor_position() == Coordinates{0, 0});
    CHECK(state.stamina() == 12u);
    CHECK(visibility_signature(state) == before_fog);

    // The first emitted event still starts the sequence at zero.
    const GameEvent event = state.move(Direction::right);
    CHECK(event.sequence == 0);
}

TEST_CASE("resting from zero restores exactly four stamina and emits one event") {
    GameState state(make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.@@@.\n"));
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 12->8
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 8->4
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 4->0
    CHECK(state.stamina() == 0u);

    const GameEvent event = state.rest();
    const RestedEvent rested = rested_of(event);
    CHECK(rested.stamina_before == 0u);
    CHECK(rested.stamina_recovered == 4u);
    CHECK(rested.stamina_after == 4u);
    CHECK(state.stamina() == 4u);
    // The fourth command consumes sequence 3, contiguous with the three moves.
    CHECK(event.sequence == 3u);
}

TEST_CASE("resting from ten restores only two and caps at twelve") {
    GameState ten(make_map("NAM-MAP 1\nwidth 3\nheight 1\nspawn 0 0\n---\n.x.\n"));
    CHECK(outcome_of(ten.move(Direction::right)).result == MoveResult::moved);  // fields: 12->10
    CHECK(ten.stamina() == 10u);

    const RestedEvent rested = rested_of(ten.rest());
    CHECK(rested.stamina_before == 10u);
    CHECK(rested.stamina_recovered == 2u);
    CHECK(rested.stamina_after == 12u);
    CHECK(ten.stamina() == 12u);
    CHECK(ten.stamina() <= GameState::maximum_stamina);
}

TEST_CASE("resting at full stamina recovers zero but still emits one event") {
    GameState state(spawn_then('.'));
    CHECK(state.stamina() == 12u);

    const GameEvent event = state.rest();
    const RestedEvent rested = rested_of(event);
    CHECK(rested.stamina_before == 12u);
    CHECK(rested.stamina_recovered == 0u);
    CHECK(rested.stamina_after == 12u);
    CHECK(state.stamina() == 12u);
    // The rest is the first command, so it starts the sequence at zero.
    CHECK(event.sequence == 0u);
}

TEST_CASE("resting from nine, ten, and eleven only tops up to the maximum") {
    struct Case {
        std::uint32_t before;
        std::uint32_t recovered;
    };
    const Case cases[] = {{9u, 3u}, {10u, 2u}, {11u, 1u}};
    for (const Case& c : cases) {
        // Build a fresh state and drain to the target starting stamina using
        // repeated open moves on a long corridor (each open step costs 1).
        GameState state(make_map("NAM-MAP 1\nwidth 6\nheight 1\nspawn 0 0\n---\n......\n"));
        while (state.stamina() > c.before) {
            CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);
        }
        REQUIRE(state.stamina() == c.before);
        const RestedEvent rested = rested_of(state.rest());
        CHECK(rested.stamina_before == c.before);
        CHECK(rested.stamina_recovered == c.recovered);
        CHECK(rested.stamina_after == 12u);
        CHECK(state.stamina() == 12u);
    }
}

TEST_CASE("resting from zero allows the next water move and leaves one stamina") {
    // Spawn beside water: from full a right move onto water costs 3. First drain
    // to zero on a mountain corridor, rest to 4, then a water step (cost 3) leaves
    // exactly 1.
    GameState state(make_map("NAM-MAP 1\nwidth 6\nheight 1\nspawn 0 0\n---\n.@@@~.\n"));
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 12->8
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 8->4
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 4->0
    CHECK(state.stamina() == 0u);

    // At zero, the next water step is unaffordable.
    CHECK(state.peek(Direction::right).result == MoveResult::blocked_by_stamina);

    const RestedEvent rested = rested_of(state.rest());
    CHECK(rested.stamina_after == 4u);

    const MoveOutcome water = outcome_of(state.move(Direction::right));  // water, cost 3.
    CHECK(water.result == MoveResult::moved);
    CHECK(water.terrain == Terrain::water);
    CHECK(water.stamina_cost == 3u);
    CHECK(state.stamina() == 1u);
}

TEST_CASE("resting preserves actor position, map serialization, and visibility") {
    GameState state(make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.@@@.\n"));
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 12->8
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 8->4

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
