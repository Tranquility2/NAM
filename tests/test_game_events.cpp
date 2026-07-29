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

// A one-row mountain corridor. Mountains cost 4 stamina and 3 daylight hours to
// enter and grant no passive recovery, so a march along it drains both meters
// without ever refusing a step.
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

TEST_CASE("daylight never blocks a walkable step") {
    // A mountain corridor spends 3 daylight hours per step so four steps fill the
    // 12-hour day. The fifth step still succeeds: daylight is spent and tracked
    // but no longer refuses a step.
    GameState state(mountain_corridor());
    for (std::uint64_t i = 0; i < 4; ++i) {
        const GameEvent step = state.move(Direction::right);
        CHECK(step.sequence == i);
        REQUIRE(payload_of(step).outcome.result == MoveResult::moved);
    }
    CHECK(state.expedition_time().daylight_hours_used == 12u);

    const GameEvent past_dusk = state.move(Direction::right);
    CHECK(past_dusk.sequence == 4u);
    const MoveOutcome& outcome = payload_of(past_dusk).outcome;
    CHECK(outcome.result == MoveResult::moved);
    CHECK(outcome.terrain == Terrain::mountain);
    CHECK(outcome.travel_hours == 3u);
    CHECK(outcome.time_after.daylight_hours_used == 15u);
    CHECK(state.actor_position() == Coordinates{5, 0});
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

TEST_CASE("an exhausted expedition is never rescued because movement never fails") {
    // Rescue was a resource failure. With fluid movement an actor that still has a
    // walkable neighbour can always continue, so wandering a water channel down to
    // an empty meter reports no ending at all.
    GameState state(make_map("NAM-MAP 1\nwidth 14\nheight 1\nspawn 0 0\n---\n~~~~~~~~~~~~~~\n"));

    Direction next = Direction::right;
    for (int step = 0; step < 40; ++step) {
        const GameEvent event = state.move(next);
        CHECK(payload_of(event).outcome.result == MoveResult::moved);
        CHECK(event.ending == ExpeditionEndingTransition::none);
        next = next == Direction::right ? Direction::left : Direction::right;
    }
    CHECK(state.stamina() == 0u);
    CHECK_FALSE(state.objective_completed());
    CHECK(state.evaluate_ending() == ExpeditionEndingTransition::none);
}

TEST_CASE("an incomplete run that camps to the deadline day ends overdue") {
    // A mountain corridor needs many provisions, so the plan allows a deadline of
    // day 7. Camping repeatedly without ever reaching the exit burns through the
    // days until the deadline day is fully spent while the objective is still
    // incomplete, so the core reports an overdue outcome.
    GameState state(make_map("NAM-MAP 1\nwidth 9\nheight 1\nspawn 0 0\n---\n.@@@@@@@@\n"));
    REQUIRE(state.deadline_day() == 7u);

    ExpeditionEndingTransition ending = ExpeditionEndingTransition::none;
    for (int night = 0; night < 10 && ending == ExpeditionEndingTransition::none; ++night) {
        // A camp is eligible only once an hour has elapsed or the meter is below
        // the cap, so each day starts with a short there-and-back march.
        (void)state.move(Direction::right);
        (void)state.move(Direction::left);
        ending = state.camp().ending;
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
    (void)state.move(Direction::right);  // discover landmark at (2,0)
    const GameEvent complete = state.move(Direction::right);  // reach exit at (3,0)

    CHECK(complete.sequence == 2u);
    CHECK(payload_of(complete).objective_update.transition ==
          ObjectiveTransition::level_completed);
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
