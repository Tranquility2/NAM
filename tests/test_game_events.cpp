#include <doctest/doctest.h>

#include <cstdint>
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

Map make_map(std::string_view text) {
    MapLoadResult result = load_map(text);
    REQUIRE(std::holds_alternative<Map>(result));
    return std::get<Map>(std::move(result));
}

// The MoveAttemptedEvent payload of an event (every core event is one today).
const MoveAttemptedEvent& payload_of(const GameEvent& event) {
    return std::get<MoveAttemptedEvent>(event.data);
}

// A 3x3 map whose spawn at the top-left can move right onto open ground, then
// bump a wall to its right, then step off the top edge — one map exercising a
// success, a terrain block, and a boundary block in a single scripted run.
Map mixed_map() {
    return make_map("NAM-MAP 1\nwidth 3\nheight 3\nspawn 0 0\n---\n..=\n...\n...\n");
}

// A 3x3 field of open ground with the spawn in a corner.
Map open_field() {
    return make_map("NAM-MAP 1\nwidth 3\nheight 3\nspawn 0 0\n---\n...\n...\n...\n");
}

// A one-row mountain corridor. From full stamina the four-cost mountains reduce
// stamina 12 -> 8 -> 4 -> 0 over three moves, so the fourth step is a typed
// stamina block that still emits one event.
Map mountain_corridor() {
    return make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.@@@@\n");
}

}  // namespace

TEST_SUITE("game") {

TEST_CASE("the first emitted event has sequence zero") {
    GameState state(open_field());
    const GameEvent first = state.move(Direction::right);
    CHECK(first.sequence == 0);
}

TEST_CASE("sequences are contiguous across success, terrain, and boundary blocks") {
    GameState state(mixed_map());

    const GameEvent moved = state.move(Direction::right);   // (0,0) -> (1,0): open
    const GameEvent terrain = state.move(Direction::right);  // (1,0) -> wall
    const GameEvent boundary = state.move(Direction::up);    // (1,0) -> off the top edge

    CHECK(moved.sequence == 0);
    CHECK(terrain.sequence == 1);
    CHECK(boundary.sequence == 2);

    CHECK(payload_of(moved).outcome.result == MoveResult::moved);
    CHECK(payload_of(terrain).outcome.result == MoveResult::blocked_by_terrain);
    CHECK(payload_of(boundary).outcome.result == MoveResult::blocked_by_boundary);
}

TEST_CASE("each command consumes exactly one contiguous sequence number") {
    GameState state(open_field());
    const Direction script[] = {Direction::right, Direction::right, Direction::up,
                                Direction::down,  Direction::left,  Direction::left};
    std::uint64_t expected = 0;
    for (const Direction direction : script) {
        const GameEvent event = state.move(direction);
        CHECK(event.sequence == expected);
        ++expected;
    }
}

TEST_CASE("an event preserves the requested direction") {
    for (const Direction direction :
         {Direction::up, Direction::down, Direction::left, Direction::right}) {
        GameState state(open_field());
        const GameEvent event = state.move(direction);
        CHECK(payload_of(event).direction == direction);
    }
}

TEST_CASE("an event's outcome equals the pure peek for the same command") {
    GameState state(mixed_map());
    for (const Direction direction : {Direction::right, Direction::down}) {
        const MoveOutcome peeked = state.peek(direction);
        const GameEvent event = state.move(direction);
        const MoveOutcome& emitted = payload_of(event).outcome;
        CHECK(emitted.result == peeked.result);
        CHECK(emitted.from == peeked.from);
        CHECK(emitted.to == peeked.to);
        CHECK(emitted.terrain == peeked.terrain);
        CHECK(emitted.stamina_cost == peeked.stamina_cost);
        CHECK(emitted.stamina_before == peeked.stamina_before);
        CHECK(emitted.stamina_after == peeked.stamina_after);
    }
}

TEST_CASE("an event preserves the destination cost and before/after stamina") {
    GameState state(mountain_corridor());
    const MoveOutcome peeked = state.peek(Direction::right);
    const GameEvent event = state.move(Direction::right);
    const MoveOutcome& emitted = payload_of(event).outcome;
    CHECK(emitted.result == MoveResult::moved);
    CHECK(emitted.terrain == Terrain::mountain);
    CHECK(emitted.stamina_cost == 4);
    CHECK(emitted.stamina_before == 12);
    CHECK(emitted.stamina_after == 8);
    // The emitted outcome still equals the immediately preceding pure peek.
    CHECK(emitted.stamina_cost == peeked.stamina_cost);
    CHECK(emitted.stamina_before == peeked.stamina_before);
    CHECK(emitted.stamina_after == peeked.stamina_after);
}

TEST_CASE("an insufficient-stamina attempt consumes exactly one sequence number") {
    GameState state(mountain_corridor());
    // Three affordable mountain steps drain stamina to zero over sequences 0..2.
    const GameEvent s0 = state.move(Direction::right);
    const GameEvent s1 = state.move(Direction::right);
    const GameEvent s2 = state.move(Direction::right);
    CHECK(s0.sequence == 0);
    CHECK(s1.sequence == 1);
    CHECK(s2.sequence == 2);
    CHECK(payload_of(s2).outcome.result == MoveResult::moved);
    CHECK(state.stamina() == 0);

    const Coordinates before = state.actor_position();
    const MoveOutcome peeked = state.peek(Direction::right);

    // The unaffordable fourth step still emits exactly one contiguous event.
    const GameEvent blocked = state.move(Direction::right);
    CHECK(blocked.sequence == 3);
    const MoveOutcome& outcome = payload_of(blocked).outcome;
    CHECK(outcome.result == MoveResult::blocked_by_stamina);
    CHECK(outcome.terrain == Terrain::mountain);
    CHECK(outcome.stamina_cost == 4);
    CHECK(outcome.stamina_before == 0);
    CHECK(outcome.stamina_after == 0);
    CHECK(outcome.stamina_cost == peeked.stamina_cost);
    CHECK(state.actor_position() == before);
    CHECK(state.stamina() == 0);

    // The next command continues the contiguous sequence with no gap.
    const GameEvent next = state.move(Direction::left);
    CHECK(next.sequence == 4);
}

TEST_CASE("a successful event is committed before it is observed") {
    GameState state(open_field());
    const GameEvent event = state.move(Direction::right);
    const MoveOutcome& outcome = payload_of(event).outcome;
    REQUIRE(outcome.result == MoveResult::moved);
    // The position is already at the destination when the event is returned.
    CHECK(state.actor_position() == outcome.to);
    CHECK(state.actor_position() == Coordinates{1, 0});
}

TEST_CASE("a blocked event leaves the actor position unchanged") {
    GameState state(mixed_map());
    (void)state.move(Direction::right);  // advance to (1,0) so a wall sits to the right.
    const Coordinates before = state.actor_position();

    const GameEvent event = state.move(Direction::right);
    CHECK(payload_of(event).outcome.result == MoveResult::blocked_by_terrain);
    CHECK(state.actor_position() == before);
}

TEST_CASE("peek emits no event and does not consume a sequence number") {
    GameState state(open_field());
    const GameEvent first = state.move(Direction::right);
    CHECK(first.sequence == 0);

    // Any number of peeks must not advance the event sequence.
    for (int i = 0; i < 5; ++i) {
        (void)state.peek(Direction::down);
        (void)state.peek(Direction::right);
    }

    const GameEvent second = state.move(Direction::down);
    CHECK(second.sequence == 1);
}

}  // TEST_SUITE("game")
