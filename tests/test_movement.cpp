#include <doctest/doctest.h>

#include <string_view>
#include <variant>

#include "coordinates.h"
#include "direction.h"
#include "game_event.h"
#include "game_state.h"
#include "map.h"
#include "map_parser.h"
#include "move_outcome.h"

namespace {

// Extract the MoveOutcome carried by a movement event, so the existing
// outcome-focused assertions read the same after the event migration.
MoveOutcome outcome_of(const GameEvent& event) {
    return std::get<MoveAttemptedEvent>(event.data).outcome;
}

// Parse a controlled map for movement tests. The fixtures on disk exercise the
// parser; here we want inline, self-describing geometry.
Map make_map(std::string_view text) {
    MapLoadResult result = load_map(text);
    REQUIRE(std::holds_alternative<Map>(result));
    return std::get<Map>(std::move(result));
}

// A 3x3 field of open ground with the spawn in the top-left corner, so an actor
// can both move freely and step off every edge.
Map open_field() {
    return make_map("NAM-MAP 1\nwidth 3\nheight 3\nspawn 0 0\n---\n...\n...\n...\n");
}

// A 3x3 map walled on all sides with a single open centre cell, so every move
// from the spawn is blocked by terrain.
Map boxed_centre() {
    return make_map("NAM-MAP 1\nwidth 3\nheight 3\nspawn 1 1\n---\n===\n|.|\n===\n");
}

}  // namespace

TEST_SUITE("game") {

TEST_CASE("a new game starts the actor on the map spawn") {
    GameState state(open_field());
    CHECK(state.actor_position() == Coordinates{0, 0});
    CHECK(state.actor_terrain() == Terrain::open);
}

TEST_CASE("a legal move commits the new position exactly once") {
    GameState state(open_field());
    const MoveOutcome outcome = outcome_of(state.move(Direction::right));
    CHECK(outcome.result == MoveResult::moved);
    CHECK(outcome.from == Coordinates{0, 0});
    CHECK(outcome.to == Coordinates{1, 0});
    CHECK(state.actor_position() == Coordinates{1, 0});
}

TEST_CASE("a move off the edge is blocked and changes nothing") {
    GameState state(open_field());  // spawn at the top-left corner.
    const Coordinates before = state.actor_position();

    const MoveOutcome up = outcome_of(state.move(Direction::up));
    CHECK(up.result == MoveResult::blocked_by_boundary);
    CHECK(up.from == before);
    CHECK(up.to == before);
    CHECK(state.actor_position() == before);

    const MoveOutcome left = outcome_of(state.move(Direction::left));
    CHECK(left.result == MoveResult::blocked_by_boundary);
    CHECK(state.actor_position() == before);
}

TEST_CASE("a move into a wall is blocked and never commits") {
    GameState state(boxed_centre());
    const Coordinates before = state.actor_position();  // (1,1)

    for (const Direction direction :
         {Direction::up, Direction::down, Direction::left, Direction::right}) {
        const MoveOutcome outcome = outcome_of(state.move(direction));
        CHECK(outcome.result == MoveResult::blocked_by_terrain);
        CHECK_FALSE(is_walkable(outcome.terrain));
        CHECK(state.actor_position() == before);
    }
}

TEST_CASE("peek reports the outcome without mutating state") {
    GameState state(open_field());
    const Coordinates before = state.actor_position();

    const MoveOutcome peeked = state.peek(Direction::right);
    CHECK(peeked.result == MoveResult::moved);
    CHECK(peeked.to == Coordinates{1, 0});
    // peek must not move the actor.
    CHECK(state.actor_position() == before);

    // A committed move matches the earlier peek.
    const MoveOutcome moved = outcome_of(state.move(Direction::right));
    CHECK(moved.result == peeked.result);
    CHECK(moved.to == peeked.to);
}

TEST_CASE("movement round-trips across the interior") {
    GameState state(open_field());
    CHECK(outcome_of(state.move(Direction::right)).result == MoveResult::moved);
    CHECK(outcome_of(state.move(Direction::down)).result == MoveResult::moved);
    CHECK(state.actor_position() == Coordinates{1, 1});
    CHECK(outcome_of(state.move(Direction::left)).result == MoveResult::moved);
    CHECK(outcome_of(state.move(Direction::up)).result == MoveResult::moved);
    CHECK(state.actor_position() == Coordinates{0, 0});
}

TEST_CASE("blocked-by-boundary reports the terrain the actor stands on") {
    GameState state(open_field());
    const MoveOutcome outcome = state.peek(Direction::up);
    CHECK(outcome.result == MoveResult::blocked_by_boundary);
    CHECK(outcome.terrain == Terrain::open);  // the spawn cell's terrain.
}

}  // TEST_SUITE("game")
