#include <doctest/doctest.h>

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

#include "direction.h"
#include "game_event.h"
#include "game_state.h"
#include "map.h"
#include "map_parser.h"
#include "move_outcome.h"

namespace {

Map make_map(std::string_view text) {
    MapLoadResult result = load_map(text);
    REQUIRE(std::holds_alternative<Map>(result));
    return std::get<Map>(std::move(result));
}

constexpr std::string_view kMap =
    "NAM-MAP 1\nwidth 5\nheight 5\nspawn 2 2\n---\n.....\n.=.=.\n.....\n.=.=.\n.....\n";

}  // namespace

TEST_SUITE("game") {

TEST_CASE("identical map and input produce identical event streams") {
    const std::vector<Direction> script{
        Direction::up,   Direction::up,    Direction::left, Direction::down,
        Direction::right, Direction::right, Direction::down, Direction::left};

    GameState a(make_map(kMap));
    GameState b(make_map(kMap));

    std::uint64_t expected_sequence = 0;
    for (const Direction direction : script) {
        const GameEvent ea = a.move(direction);
        const GameEvent eb = b.move(direction);
        const MoveAttemptedEvent& pa = std::get<MoveAttemptedEvent>(ea.data);
        const MoveAttemptedEvent& pb = std::get<MoveAttemptedEvent>(eb.data);

        // Sequence numbers advance contiguously and identically on both games.
        CHECK(ea.sequence == expected_sequence);
        CHECK(eb.sequence == expected_sequence);
        ++expected_sequence;

        CHECK(pa.direction == direction);
        CHECK(pa.direction == pb.direction);
        CHECK(pa.outcome.result == pb.outcome.result);
        CHECK(pa.outcome.from == pb.outcome.from);
        CHECK(pa.outcome.to == pb.outcome.to);
        CHECK(pa.outcome.terrain == pb.outcome.terrain);
        CHECK(pa.outcome.stamina_cost == pb.outcome.stamina_cost);
        CHECK(pa.outcome.stamina_before == pb.outcome.stamina_before);
        CHECK(pa.outcome.stamina_after == pb.outcome.stamina_after);

        // Current stamina stays identical after every command in both games.
        CHECK(a.stamina() == b.stamina());
    }

    CHECK(a.actor_position() == b.actor_position());
    CHECK(a.stamina() == b.stamina());
    CHECK(a.render() == b.render());
}

TEST_CASE("peek is a pure function of state and direction") {
    GameState state(make_map(kMap));
    const MoveOutcome first = state.peek(Direction::up);
    const MoveOutcome second = state.peek(Direction::up);
    CHECK(first.result == second.result);
    CHECK(first.to == second.to);
    CHECK(first.terrain == second.terrain);
    // The repeated peeks left the actor untouched.
    CHECK(state.actor_position() == Coordinates{2, 2});
}

}  // TEST_SUITE("game")
