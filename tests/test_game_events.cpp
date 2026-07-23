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

const RestedEvent& rested_of(const GameEvent& event) {
    return std::get<RestedEvent>(event.data);
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

TEST_CASE("a movement, a rest, and a movement consume contiguous sequence numbers") {
    GameState state(mountain_corridor());
    const GameEvent moved = state.move(Direction::right);        // mountain, 12->8.
    const GameEvent rested = state.rest();                       // 8->12.
    const GameEvent moved_again = state.move(Direction::right);  // mountain, 12->8.

    CHECK(moved.sequence == 0);
    CHECK(rested.sequence == 1);
    CHECK(moved_again.sequence == 2);

    CHECK(std::holds_alternative<MoveAttemptedEvent>(moved.data));
    CHECK(std::holds_alternative<RestedEvent>(rested.data));
    CHECK(std::holds_alternative<MoveAttemptedEvent>(moved_again.data));

    CHECK(payload_of(moved).outcome.result == MoveResult::moved);
    CHECK(payload_of(moved).outcome.stamina_after == 8);

    const RestedEvent& rest_payload = rested_of(rested);
    CHECK(rest_payload.stamina_before == 8);
    CHECK(rest_payload.stamina_recovered == 4);
    CHECK(rest_payload.stamina_after == 12);

    CHECK(payload_of(moved_again).outcome.result == MoveResult::moved);
    CHECK(payload_of(moved_again).outcome.stamina_before == 12);
    CHECK(payload_of(moved_again).outcome.stamina_after == 8);
}

TEST_CASE("a rest at full stamina still consumes exactly one sequence number") {
    GameState state(open_field());
    const GameEvent rested = state.rest();
    CHECK(rested.sequence == 0);
    const RestedEvent& payload = rested_of(rested);
    CHECK(payload.stamina_before == 12);
    CHECK(payload.stamina_recovered == 0);
    CHECK(payload.stamina_after == 12);

    // The next command continues the contiguous sequence with no gap.
    const GameEvent next = state.move(Direction::right);
    CHECK(next.sequence == 1);
}

TEST_CASE("two-field movement-event aggregate construction keeps a default objective update") {
    // REQ-015: adding the nested ObjectiveUpdate must not break existing
    // two-field aggregate initialization of a movement event. The third member is
    // value-initialized: equal seeking before/after and no transition.
    const MoveOutcome outcome{MoveResult::moved, {0, 0}, {1, 0}, Terrain::open, 1, 12, 11};
    const MoveAttemptedEvent event{Direction::right, outcome};
    CHECK(event.objective_update.before == ObjectiveStatus::seeking_beacon);
    CHECK(event.objective_update.after == ObjectiveStatus::seeking_beacon);
    CHECK(event.objective_update.transition == ObjectiveTransition::none);
}

TEST_CASE("every movement event carries the exact objective update in one contiguous stream") {
    // A one-row corridor whose beacon is the far cell (4,0): walking out and back
    // produces one ordered event per command, each nesting the exact before/after
    // status and the typed transition for that command.
    GameState state(make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n"));
    REQUIRE(state.objective().beacon == Coordinates{4, 0});

    std::uint64_t expected_sequence = 0;
    const auto check_move = [&](Direction direction, ObjectiveStatus before, ObjectiveStatus after,
                                ObjectiveTransition transition) {
        const GameEvent event = state.move(direction);
        CHECK(event.sequence == expected_sequence);
        ++expected_sequence;
        const MoveAttemptedEvent& move = payload_of(event);
        CHECK(move.objective_update.before == before);
        CHECK(move.objective_update.after == after);
        CHECK(move.objective_update.transition == transition);
    };

    using S = ObjectiveStatus;
    using T = ObjectiveTransition;
    check_move(Direction::right, S::seeking_beacon, S::seeking_beacon, T::none);
    check_move(Direction::right, S::seeking_beacon, S::seeking_beacon, T::none);
    check_move(Direction::right, S::seeking_beacon, S::seeking_beacon, T::none);
    check_move(Direction::right, S::seeking_beacon, S::returning_to_spawn, T::beacon_discovered);
    check_move(Direction::left, S::returning_to_spawn, S::returning_to_spawn, T::none);
    check_move(Direction::left, S::returning_to_spawn, S::returning_to_spawn, T::none);
    check_move(Direction::left, S::returning_to_spawn, S::returning_to_spawn, T::none);
    check_move(Direction::left, S::returning_to_spawn, S::completed, T::expedition_completed);
    CHECK(state.objective_completed());
}

TEST_CASE("a blocked movement event reports no objective transition") {
    GameState state(mixed_map());
    (void)state.move(Direction::right);  // advance to (1,0) so a wall sits to the right.
    const GameEvent blocked = state.move(Direction::right);
    CHECK(payload_of(blocked).outcome.result == MoveResult::blocked_by_terrain);
    CHECK(payload_of(blocked).objective_update.transition == ObjectiveTransition::none);
    CHECK(payload_of(blocked).objective_update.before ==
          payload_of(blocked).objective_update.after);
}

}  // TEST_SUITE("game")
