#include <doctest/doctest.h>

#include <string_view>
#include <variant>
#include <vector>

#include "direction.h"
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

    for (const Direction direction : script) {
        const MoveOutcome oa = a.move(direction);
        const MoveOutcome ob = b.move(direction);
        CHECK(oa.result == ob.result);
        CHECK(oa.from == ob.from);
        CHECK(oa.to == ob.to);
        CHECK(oa.terrain == ob.terrain);
    }

    CHECK(a.actor_position() == b.actor_position());
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
