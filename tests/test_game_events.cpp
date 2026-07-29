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

// A one-row mountain corridor. Mountains cost 4 stamina to enter and grant no
// passive recovery, so a march along it drains the meter without ever refusing a
// step.
Map mountain_corridor() {
    return make_map("NAM-MAP 1\nwidth 7\nheight 1\nspawn 0 0\n---\n.@@@@@@\n");
}

}  // namespace

TEST_SUITE("game") {

TEST_CASE("sequences are contiguous across success terrain and boundary blocks") {
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

TEST_CASE("the first emitted event has sequence zero") {
    GameState state(open_field());
    const GameEvent first = state.move(Direction::right);
    CHECK(first.sequence == 0);
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

TEST_CASE("an event outcome equals the pure peek for the same command") {
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

TEST_CASE("an event preserves the destination cost and before after stamina") {
    GameState state(mountain_corridor());
    const MoveOutcome peeked = state.peek(Direction::right);
    const GameEvent event = state.move(Direction::right);
    const MoveOutcome& emitted = payload_of(event).outcome;
    CHECK(emitted.result == MoveResult::moved);
    CHECK(emitted.terrain == Terrain::mountain);
    CHECK(emitted.stamina_cost == 4);
    CHECK(emitted.stamina_before == 20);
    CHECK(emitted.stamina_after == 16);
    // The emitted outcome still equals the immediately preceding pure peek.
    CHECK(emitted.stamina_cost == peeked.stamina_cost);
    CHECK(emitted.stamina_before == peeked.stamina_before);
    CHECK(emitted.stamina_after == peeked.stamina_after);
}

TEST_CASE("stamina never blocks a walkable step") {
    // A two-cell water channel drains 3 stamina per step and grants no passive
    // recovery, so the meter saturates at zero. Movement stays fluid: the step
    // taken at zero stamina still succeeds and still consumes exactly one
    // contiguous sequence number.
    GameState state(make_map("NAM-MAP 1\nwidth 2\nheight 1\nspawn 0 0\n---\n~~\n"));
    Direction next = Direction::right;
    for (std::uint64_t i = 0; i < 7; ++i) {
        const GameEvent step = state.move(next);
        CHECK(step.sequence == i);
        REQUIRE(payload_of(step).outcome.result == MoveResult::moved);
        next = next == Direction::right ? Direction::left : Direction::right;
    }
    CHECK(state.stamina() == 0);

    const GameEvent exhausted = state.move(next);
    CHECK(exhausted.sequence == 7);
    const MoveOutcome& outcome = payload_of(exhausted).outcome;
    CHECK(outcome.result == MoveResult::moved);
    CHECK(outcome.terrain == Terrain::water);
    CHECK(outcome.stamina_cost == 3);
    CHECK(outcome.stamina_before == 0);
    CHECK(outcome.stamina_recovered == 0);
    CHECK(outcome.stamina_after == 0);
    CHECK(state.actor_position() == Coordinates{0, 0});

    // The next command continues the contiguous sequence with no gap.
    const GameEvent following = state.move(Direction::right);
    CHECK(following.sequence == 8);
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

TEST_CASE("two-field movement-event aggregate construction keeps a default objective update") {
    // REQ-015: adding the nested ObjectiveUpdate must not break existing
    // two-field aggregate initialization of a movement event. The third member is
    // value-initialized: equal seeking before/after and no transition.
    const MoveOutcome outcome{MoveResult::moved, {0, 0}, {1, 0}, Terrain::open, 1, 20, 19};
    const MoveAttemptedEvent event{Direction::right, outcome};
    CHECK(event.objective_update.before == ObjectiveStatus::seeking_landmark);
    CHECK(event.objective_update.after == ObjectiveStatus::seeking_landmark);
    CHECK(event.objective_update.transition == ObjectiveTransition::none);
}

TEST_CASE("every movement event carries the exact objective update in one contiguous stream") {
    // A one-row corridor whose landmark is (2,0) and exit is (3,0): walking
    // forward produces one ordered event per command, each nesting
    // the exact before/after status and the typed transition for that command.
    GameState state(make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n"));
    REQUIRE(state.objective().landmark == Coordinates{2, 0});
    REQUIRE(state.objective().beacon == Coordinates{3, 0});

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
    check_move(Direction::right, S::seeking_landmark, S::seeking_landmark, T::none);
    check_move(Direction::right, S::seeking_landmark, S::seeking_exit, T::landmark_discovered);
    check_move(Direction::right, S::seeking_exit, S::completed, T::level_completed);
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
