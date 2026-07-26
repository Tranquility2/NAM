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
// stamina 20 -> 16 -> 12 -> 8 -> 4 -> 0 over five moves, so the sixth step is a
// typed stamina block that still emits one event.
Map mountain_corridor() {
    return make_map("NAM-MAP 1\nwidth 7\nheight 1\nspawn 0 0\n---\n.@@@@@@\n");
}

}  // namespace

TEST_SUITE("game") {

TEST_CASE("the first emitted event has sequence zero") {
    GameState state(open_field());
    const GameEvent first = state.move(Direction::right);
    CHECK(first.sequence == 0);
    CHECK(first.ending == ExpeditionEndingTransition::none);
}

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

TEST_CASE("an insufficient-stamina attempt consumes exactly one sequence number") {
    // A fields corridor drains 2 stamina and 1 daylight hour per step, so ten steps
    // reach zero stamina after ten daylight hours (still within the 12-hour day).
    GameState state(make_map("NAM-MAP 1\nwidth 12\nheight 1\nspawn 0 0\n---\n.xxxxxxxxxxx\n"));
    for (std::uint64_t i = 0; i < 10; ++i) {
        const GameEvent step = state.move(Direction::right);
        CHECK(step.sequence == i);
        CHECK(payload_of(step).outcome.result == MoveResult::moved);
    }
    CHECK(state.stamina() == 0);

    const Coordinates before = state.actor_position();
    const MoveOutcome peeked = state.peek(Direction::right);
    // Daylight still fits a one-hour fields step, so the block is by stamina.
    REQUIRE(peeked.result == MoveResult::blocked_by_stamina);

    // The unaffordable eleventh step still emits exactly one contiguous event.
    const GameEvent blocked = state.move(Direction::right);
    CHECK(blocked.sequence == 10);
    const MoveOutcome& outcome = payload_of(blocked).outcome;
    CHECK(outcome.result == MoveResult::blocked_by_stamina);
    CHECK(outcome.terrain == Terrain::fields);
    CHECK(outcome.stamina_cost == 2);
    CHECK(outcome.stamina_before == 0);
    CHECK(outcome.stamina_after == 0);
    CHECK(outcome.stamina_cost == peeked.stamina_cost);
    CHECK(state.actor_position() == before);
    CHECK(state.stamina() == 0);

    // The next command continues the contiguous sequence with no gap.
    const GameEvent next = state.move(Direction::left);
    CHECK(next.sequence == 11);
}

TEST_CASE("a move blocked by insufficient daylight consumes one contiguous sequence number") {
    // A mountain corridor: each mountain step costs 3 daylight hours, so four steps
    // fill the 12-hour day. The fifth step is blocked by daylight, not stamina,
    // because daylight is validated first (REQ-004).
    GameState state(mountain_corridor());
    for (std::uint64_t i = 0; i < 4; ++i) {
        const GameEvent step = state.move(Direction::right);  // 3 hours, -4 stamina each
        CHECK(step.sequence == i);
        REQUIRE(payload_of(step).outcome.result == MoveResult::moved);
    }
    CHECK(state.expedition_time().daylight_hours_used == 12u);
    CHECK(state.stamina() == 4u);  // still enough stamina for one more mountain step

    const GameEvent blocked = state.move(Direction::right);
    CHECK(blocked.sequence == 4u);
    const MoveOutcome& outcome = payload_of(blocked).outcome;
    CHECK(outcome.result == MoveResult::blocked_by_daylight);
    CHECK(outcome.terrain == Terrain::mountain);
    CHECK(outcome.travel_hours == 3u);
    CHECK(outcome.stamina_before == outcome.stamina_after);
    CHECK(outcome.time_before == outcome.time_after);
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

TEST_CASE("a movement a rest and a movement consume contiguous sequence numbers") {
    GameState state(mountain_corridor());
    const GameEvent moved = state.move(Direction::right);        // mountain, 20->16.
    const GameEvent rested = state.rest();                       // mountain, 16->18.
    const GameEvent moved_again = state.move(Direction::right);  // mountain, 18->14.

    CHECK(moved.sequence == 0);
    CHECK(rested.sequence == 1);
    CHECK(moved_again.sequence == 2);

    CHECK(std::holds_alternative<MoveAttemptedEvent>(moved.data));
    CHECK(std::holds_alternative<RestedEvent>(rested.data));
    CHECK(std::holds_alternative<MoveAttemptedEvent>(moved_again.data));

    CHECK(payload_of(moved).outcome.result == MoveResult::moved);
    CHECK(payload_of(moved).outcome.stamina_after == 16);

    const RestedEvent& rest_payload = rested_of(rested);
    CHECK(rest_payload.result == RestResult::recovered);
    CHECK(rest_payload.terrain == Terrain::mountain);
    CHECK(rest_payload.stamina_before == 16);
    CHECK(rest_payload.stamina_recovered == 2);
    CHECK(rest_payload.stamina_after == 18);
    CHECK(rest_payload.provisions_before == rest_payload.provisions_after + 1u);

    CHECK(payload_of(moved_again).outcome.result == MoveResult::moved);
    CHECK(payload_of(moved_again).outcome.stamina_before == 18);
    CHECK(payload_of(moved_again).outcome.stamina_after == 14);
}

TEST_CASE("a rest at full stamina still consumes exactly one sequence number") {
    GameState state(open_field());
    const std::uint32_t provisions_before = state.provisions();
    const GameEvent rested = state.rest();
    CHECK(rested.sequence == 0);
    const RestedEvent& payload = rested_of(rested);
    CHECK(payload.result == RestResult::already_full);
    CHECK(payload.stamina_before == 20);
    CHECK(payload.stamina_recovered == 0);
    CHECK(payload.stamina_after == 20);
    CHECK(payload.provisions_before == provisions_before);
    CHECK(payload.provisions_after == provisions_before);

    // The next command continues the contiguous sequence with no gap.
    const GameEvent next = state.move(Direction::right);
    CHECK(next.sequence == 1);
}

TEST_CASE("an incomplete run before the deadline ends in a rescue transition") {
    // A wide open corridor: spawn (0,0), beacon (10,0), minimum 2 days, deadline
    // day 4, starting provisions 2. Oscillating x in {0,1,2} without ever reaching
    // the beacon, camping whenever the day is spent, eventually runs out of
    // provisions on a day before the deadline with no move, rest, or camp
    // continuation, so the core reports a rescue.
    GameState state(make_map("NAM-MAP 1\nwidth 14\nheight 1\nspawn 0 0\n---\n..............\n"));
    REQUIRE(state.deadline_day() == 4u);

    ExpeditionEndingTransition ending = ExpeditionEndingTransition::none;
    std::uint64_t expected_sequence = 0;
    for (int step = 0; step < 200 && ending == ExpeditionEndingTransition::none; ++step) {
        const Coordinates position = state.actor_position();
        const Direction direction = position.x <= 0
                                        ? Direction::right
                                        : (position.x >= 2 ? Direction::left
                                                           : (step % 2 == 0 ? Direction::right
                                                                            : Direction::left));
        GameEvent event = state.peek(direction).result == MoveResult::moved
                              ? state.move(direction)
                              : state.camp();
        CHECK(event.sequence == expected_sequence);
        ++expected_sequence;
        ending = event.ending;
    }
    CHECK(ending == ExpeditionEndingTransition::rescued);
    CHECK_FALSE(state.objective_completed());
    CHECK(state.expedition_time().day < state.deadline_day());
    CHECK(state.evaluate_ending() == ExpeditionEndingTransition::rescued);
}

TEST_CASE("an incomplete run on the deadline day ends in an overdue transition") {
    // A mountain corridor forces expensive bivouacs and rests, so the minimum plan
    // needs many provisions (starting 10) and a deadline of day 7. Oscillating near
    // spawn without reaching the beacon and camping when the day is spent burns
    // through days until the deadline day is fully spent while the objective is
    // still incomplete, so the core reports an overdue outcome, not a rescue.
    GameState state(make_map("NAM-MAP 1\nwidth 9\nheight 1\nspawn 0 0\n---\n.@@@@@@@@\n"));
    REQUIRE(state.deadline_day() == 7u);

    ExpeditionEndingTransition ending = ExpeditionEndingTransition::none;
    for (int step = 0; step < 400 && ending == ExpeditionEndingTransition::none; ++step) {
        const Direction direction =
            state.actor_position().x <= 0 ? Direction::right : Direction::left;
        GameEvent event = state.peek(direction).result == MoveResult::moved
                              ? state.move(direction)
                              : state.camp();
        ending = event.ending;
    }
    CHECK(ending == ExpeditionEndingTransition::overdue);
    CHECK_FALSE(state.objective_completed());
    CHECK(state.expedition_time().day == state.deadline_day());
}

TEST_CASE("completion takes precedence and reports no ending transition") {
    // Completion always wins over every failure: the completing move reports no
    // ending transition even after the run is complete.
    GameState state(make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n"));
    (void)state.move(Direction::right);
    (void)state.move(Direction::right);
    (void)state.move(Direction::right);  // discover beacon at (3,0)
    (void)state.move(Direction::left);
    (void)state.move(Direction::left);
    const GameEvent complete = state.move(Direction::left);

    CHECK(complete.sequence == 5u);
    CHECK(payload_of(complete).objective_update.transition ==
          ObjectiveTransition::expedition_completed);
    CHECK(complete.ending == ExpeditionEndingTransition::none);
    CHECK(state.objective_completed());
    CHECK(state.evaluate_ending() == ExpeditionEndingTransition::none);
}

TEST_CASE("a successful camp emits one typed camp event and advances the day") {
    GameState state(make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n"));
    const GameEvent moved = state.move(Direction::right);  // day 1, 1 hour used
    CHECK(moved.sequence == 0u);
    const std::uint32_t provisions_before = state.provisions();

    const GameEvent camped = state.camp();
    CHECK(camped.sequence == 1u);
    REQUIRE(std::holds_alternative<CampedEvent>(camped.data));
    const CampedEvent& payload = std::get<CampedEvent>(camped.data);
    CHECK(payload.result == CampResult::camped);
    CHECK(payload.kind == CampKind::normal);
    CHECK(payload.time.before.day == 1u);
    CHECK(payload.time.after.day == 2u);
    CHECK(payload.time.after.daylight_hours_used == 0u);
    CHECK(payload.stamina_after == 20u);
    CHECK(payload.provision_cost == 1u);
    CHECK(payload.provisions_after + 1u == provisions_before);
    CHECK(state.expedition_time().day == 2u);
    CHECK(state.stamina() == 20u);
}

TEST_CASE("an ineligible camp changes no state and still consumes a sequence number") {
    // At spawn on day 1 with full stamina and zero daylight used, a camp is not yet
    // eligible: it changes nothing but still emits one contiguous event.
    GameState state(make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n"));
    const std::uint32_t provisions_before = state.provisions();

    const GameEvent camped = state.camp();
    CHECK(camped.sequence == 0u);
    const CampedEvent& payload = std::get<CampedEvent>(camped.data);
    CHECK(payload.result == CampResult::ineligible);
    CHECK(state.expedition_time().day == 1u);
    CHECK(state.expedition_time().daylight_hours_used == 0u);
    CHECK(state.provisions() == provisions_before);

    const GameEvent next = state.move(Direction::right);
    CHECK(next.sequence == 1u);
}

TEST_CASE("a bivouac on water sets stamina to ten and costs two provisions") {
    // A water corridor with enough starting provisions (3) to afford a bivouac.
    // Move onto the water, then bivouac: it costs two provisions and sets stamina
    // to exactly 10.
    GameState state(make_map("NAM-MAP 1\nwidth 6\nheight 1\nspawn 0 0\n---\n.~~~~~\n"));
    const GameEvent moved = state.move(Direction::right);  // onto water (2 hours, -3 stamina)
    REQUIRE(std::get<MoveAttemptedEvent>(moved.data).outcome.result == MoveResult::moved);
    REQUIRE(state.actor_terrain() == Terrain::water);
    const std::uint32_t provisions_before = state.provisions();

    const GameEvent camped = state.camp();
    const CampedEvent& payload = std::get<CampedEvent>(camped.data);
    CHECK(payload.result == CampResult::camped);
    CHECK(payload.kind == CampKind::bivouac);
    CHECK(payload.provision_cost == 2u);
    CHECK(payload.stamina_after == 10u);
    CHECK(state.stamina() == 10u);
    CHECK(payload.provisions_after + 2u == provisions_before);
}

TEST_CASE("two-field movement-event aggregate construction keeps a default objective update") {
    // REQ-015: adding the nested ObjectiveUpdate must not break existing
    // two-field aggregate initialization of a movement event. The third member is
    // value-initialized: equal seeking before/after and no transition.
    const MoveOutcome outcome{MoveResult::moved, {0, 0}, {1, 0}, Terrain::open, 1, 20, 19};
    const MoveAttemptedEvent event{Direction::right, outcome};
    CHECK(event.objective_update.before == ObjectiveStatus::seeking_beacon);
    CHECK(event.objective_update.after == ObjectiveStatus::seeking_beacon);
    CHECK(event.objective_update.transition == ObjectiveTransition::none);
}

TEST_CASE("every movement event carries the exact objective update in one contiguous stream") {
    // A one-row corridor whose beacon is the distant scenic-fallback cell (3,0):
    // walking out and back produces one ordered event per command, each nesting
    // the exact before/after status and the typed transition for that command.
    GameState state(make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n"));
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
    check_move(Direction::right, S::seeking_beacon, S::seeking_beacon, T::none);
    check_move(Direction::right, S::seeking_beacon, S::seeking_beacon, T::none);
    check_move(Direction::right, S::seeking_beacon, S::returning_to_spawn, T::beacon_discovered);
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
