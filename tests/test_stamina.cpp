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

TEST_CASE("successful movement onto each terrain charges its cost then its recovery") {
    struct Case {
        char symbol;
        Terrain terrain;
        std::uint32_t cost;
        std::uint32_t recovered;
        std::uint32_t after;
    };
    // From the cap, the passive recovery is clamped by the headroom the charge
    // just opened, so easy ground is effectively free and rough ground drains.
    const Case cases[] = {
        {'.', Terrain::open, 1u, 1u, 20u},
        {'x', Terrain::fields, 2u, 2u, 20u},
        {'^', Terrain::hill, 2u, 1u, 19u},
        {'~', Terrain::water, 3u, 0u, 17u},
        {'@', Terrain::mountain, 4u, 0u, 16u},
    };

    for (const Case& c : cases) {
        GameState state(spawn_then(c.symbol));
        const MoveOutcome outcome = outcome_of(state.move(Direction::right));
        CHECK(outcome.result == MoveResult::moved);
        CHECK(outcome.terrain == c.terrain);
        CHECK(outcome.stamina_cost == c.cost);
        CHECK(outcome.stamina_recovered == c.recovered);
        CHECK(outcome.stamina_before == 20u);
        CHECK(outcome.stamina_after == c.after);
        CHECK(outcome.to == Coordinates{1, 0});
        CHECK(state.actor_position() == Coordinates{1, 0});
        CHECK(state.stamina() == c.after);
    }
}

TEST_CASE("open ground gives back more stamina than it charges") {
    // Stepping onto the mountain drains four; stepping back onto open ground
    // charges one and immediately returns two, so easy travel is restorative.
    GameState state(make_map("NAM-MAP 1\nwidth 4\nheight 1\nspawn 0 0\n---\n.@..\n"));

    const MoveOutcome onto_mountain = outcome_of(state.move(Direction::right));
    CHECK(onto_mountain.result == MoveResult::moved);
    CHECK(onto_mountain.stamina_cost == 4u);
    CHECK(onto_mountain.stamina_recovered == 0u);
    CHECK(onto_mountain.stamina_after == 16u);

    const MoveOutcome back_to_open = outcome_of(state.move(Direction::left));
    CHECK(back_to_open.result == MoveResult::moved);
    CHECK(back_to_open.stamina_cost == 1u);
    CHECK(back_to_open.stamina_recovered == 2u);
    CHECK(back_to_open.stamina_after == 17u);
    CHECK(state.stamina() == 17u);
}

TEST_CASE("reaching the level landmark restores the meter completely") {
    // The landmark is the safe waypoint of the level: entering it for the first
    // time refills the meter no matter how drained the approach was.
    GameState state(make_map("NAM-MAP 1\nwidth 4\nheight 1\nspawn 0 0\n---\n.@@.\n"));
    REQUIRE(state.objective().landmark == Coordinates{2, 0});

    CHECK(outcome_of(state.move(Direction::right)).stamina_after == 16u);

    const MoveOutcome onto_landmark = outcome_of(state.move(Direction::right));
    CHECK(onto_landmark.result == MoveResult::moved);
    CHECK(onto_landmark.stamina_cost == 4u);
    CHECK(onto_landmark.stamina_before == 16u);
    CHECK(onto_landmark.stamina_recovered == 8u);
    CHECK(onto_landmark.stamina_after == 20u);
    CHECK(state.stamina() == 20u);
}

TEST_CASE("the stamina charge saturates at zero instead of refusing a step") {
    // A two-cell water channel charges three per step and recovers nothing, so
    // the meter empties and then stays empty while travel continues.
    GameState state(make_map("NAM-MAP 1\nwidth 2\nheight 1\nspawn 0 0\n---\n~~\n"));

    Direction next = Direction::right;
    for (int i = 0; i < 7; ++i) {
        CHECK(outcome_of(state.move(next)).result == MoveResult::moved);
        next = next == Direction::right ? Direction::left : Direction::right;
    }
    CHECK(state.stamina() == 0u);

    const MoveOutcome exhausted = outcome_of(state.move(next));
    CHECK(exhausted.result == MoveResult::moved);
    CHECK(exhausted.terrain == Terrain::water);
    CHECK(exhausted.stamina_cost == 3u);
    CHECK(exhausted.stamina_before == 0u);
    CHECK(exhausted.stamina_recovered == 0u);
    CHECK(exhausted.stamina_after == 0u);
    CHECK(state.stamina() == 0u);
    CHECK(state.actor_position() == Coordinates{0, 0});
}

TEST_CASE("boundary and wall blocks cost zero at non-full stamina") {
    GameState state(make_map("NAM-MAP 1\nwidth 3\nheight 1\nspawn 0 0\n---\n.@=\n"));

    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // mountain, 20->16
    CHECK(state.stamina() == 16u);

    const MoveOutcome wall = outcome_of(state.move(Direction::right));  // wall
    CHECK(wall.result == MoveResult::blocked_by_terrain);
    CHECK(wall.stamina_cost == 0u);
    CHECK(wall.stamina_before == 16u);
    CHECK(wall.stamina_after == 16u);

    const MoveOutcome edge = outcome_of(state.move(Direction::up));  // off the top edge
    CHECK(edge.result == MoveResult::blocked_by_boundary);
    CHECK(edge.stamina_cost == 0u);
    CHECK(edge.stamina_before == 16u);
    CHECK(edge.stamina_after == 16u);

    CHECK(state.stamina() == 16u);
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
    GameState state(make_map("NAM-MAP 1\nwidth 4\nheight 1\nspawn 0 0\n---\n.@..\n"));
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
    // Spawn on fields and drain onto the neighbouring mountain: the step back
    // onto fields charges two and returns two, so the meter holds at 16.
    GameState state(make_map("NAM-MAP 1\nwidth 4\nheight 1\nspawn 0 0\n---\nx@..\n"));
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 20->16
    CHECK(outcome_of(state.move(Direction::left)).result == MoveResult::moved);   // holds at 16
    REQUIRE(state.actor_terrain() == Terrain::fields);
    REQUIRE(state.stamina() == 16u);
    const std::uint32_t provisions_before = state.provisions();

    const RestedEvent rested = rested_of(state.rest());
    CHECK(rested.result == RestResult::recovered);
    CHECK(rested.terrain == Terrain::fields);
    CHECK(rested.stamina_before == 16u);
    CHECK(rested.stamina_recovered == 4u);  // fields recover 6, but cap is 20.
    CHECK(rested.stamina_after == 20u);
    CHECK(rested.provisions_before == provisions_before);
    CHECK(rested.provisions_after + 1u == provisions_before);
    CHECK(state.stamina() == 20u);
    CHECK(state.stamina() <= GameState::maximum_stamina);
}

TEST_CASE("resting from seventeen eighteen and nineteen only tops up to the maximum") {
    struct Case {
        std::uint32_t moves;
        std::uint32_t before;
        std::uint32_t recovered;
    };
    // The mountain at index 1 drains the meter to 16 and each following open step
    // nets one back, so a short approach stands on open ground at 17, 18, or 19.
    const Case cases[] = {{2u, 17u, 3u}, {3u, 18u, 2u}, {4u, 19u, 1u}};
    for (const Case& c : cases) {
        GameState state(
            make_map("NAM-MAP 1\nwidth 16\nheight 1\nspawn 0 0\n---\n.@..............\n"));
        for (std::uint32_t i = 0; i < c.moves; ++i) {
            CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);
        }
        REQUIRE(state.actor_terrain() == Terrain::open);
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
    GameState state(make_map("NAM-MAP 1\nwidth 4\nheight 1\nspawn 0 0\n---\n.@..\n"));
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

TEST_CASE("resting is blocked when too little daylight remains") {
    // Five water steps spend ten daylight hours and fifteen stamina; one open step
    // spends the eleventh hour, leaving one hour of daylight — fewer than the two
    // an emergency rest needs. Stamina is below the cap and a provision remains, so
    // the rest is blocked purely by daylight and changes no state, still emitting
    // exactly one event.
    GameState state(
        make_map("NAM-MAP 1\nwidth 20\nheight 1\nspawn 0 0\n---\n.~~~~~..............\n"));
    for (int i = 0; i < 6; ++i) {
        REQUIRE(outcome_of(state.move(Direction::right)).result == MoveResult::moved);
    }
    CHECK(state.expedition_time().daylight_hours_used == 11u);
    CHECK(state.stamina() == 6u);
    REQUIRE(state.provisions() >= 1u);

    const std::uint32_t provisions_before = state.provisions();
    const std::uint32_t stamina_before = state.stamina();
    const ExpeditionTime time_before = state.expedition_time();

    const RestedEvent rested = rested_of(state.rest());
    CHECK(rested.result == RestResult::blocked_by_daylight);
    CHECK(rested.stamina_before == stamina_before);
    CHECK(rested.stamina_after == stamina_before);
    CHECK(rested.provisions_before == provisions_before);
    CHECK(rested.provisions_after == provisions_before);
    CHECK(rested.time.before == time_before);
    CHECK(rested.time.after == time_before);
    CHECK(state.stamina() == stamina_before);
    CHECK(state.provisions() == provisions_before);
    CHECK(state.expedition_time() == time_before);
}

TEST_CASE("resting consumes two daylight hours") {
    // A single water step spends two daylight hours and three stamina; a following
    // rest recovers stamina, spends a provision, and consumes exactly two more
    // daylight hours.
    GameState state(make_map("NAM-MAP 1\nwidth 4\nheight 1\nspawn 0 0\n---\n.~..\n"));
    REQUIRE(outcome_of(state.move(Direction::right)).result == MoveResult::moved);  // 20->17
    REQUIRE(state.actor_terrain() == Terrain::water);
    CHECK(state.expedition_time().daylight_hours_used == 2u);

    const RestedEvent rested = rested_of(state.rest());
    CHECK(rested.result == RestResult::recovered);
    CHECK(rested.stamina_after == 18u);  // water recovers 1.
    CHECK(rested.time.before.daylight_hours_used == 2u);
    CHECK(rested.time.after.daylight_hours_used == 4u);
    CHECK(state.expedition_time().daylight_hours_used == 4u);
}

TEST_CASE("resting preserves actor position map serialization and visibility") {
    GameState state(make_map("NAM-MAP 1\nwidth 4\nheight 1\nspawn 0 0\n---\n.@..\n"));
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
