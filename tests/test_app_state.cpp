#include <doctest/doctest.h>

#include <cstdint>
#include <string>

#include "app_state.h"
#include "coordinates.h"
#include "direction.h"
#include "game_event.h"
#include "messages.h"
#include "move_outcome.h"
#include "terrain.h"

using namespace nam::console;

namespace {

MoveOutcome moved_to(Coordinates from, Coordinates to, Terrain terrain) {
    const std::uint32_t cost = stamina_cost_of(terrain).value_or(0);
    return MoveOutcome{MoveResult::moved, from, to, terrain, cost, 12, 12 - cost};
}

MoveOutcome blocked(Coordinates at, Terrain terrain) {
    return MoveOutcome{MoveResult::blocked_by_terrain, at, at, terrain, 0, 12, 12};
}

MoveOutcome stamina_blocked(Coordinates at, Terrain terrain, std::uint32_t cost,
                            std::uint32_t stamina) {
    return MoveOutcome{MoveResult::blocked_by_stamina, at, at, terrain, cost, stamina, stamina};
}

// Wrap a direction and outcome into the movement event the HUD now consumes. The
// sequence is immaterial to HUD behaviour (it is not displayed yet), so a
// running counter keeps the events well-formed without the tests depending on it.
GameEvent move_event(Direction direction, const MoveOutcome& outcome, std::uint64_t sequence = 0) {
    return GameEvent{sequence, MoveAttemptedEvent{direction, outcome}};
}

// Wrap a rest transition into the rest event the HUD now also consumes.
GameEvent rest_event(std::uint32_t before, std::uint32_t recovered, std::uint64_t sequence = 0) {
    return GameEvent{sequence, RestedEvent{before, recovered, before + recovered}};
}

}  // namespace

TEST_SUITE("console") {

TEST_CASE("attempts always count while only successful moves advance the move count") {
    Hud hud;
    hud.record_event(move_event(Direction::right, moved_to({0, 0}, {1, 0}, Terrain::open), 0));
    hud.record_event(move_event(Direction::up, blocked({1, 0}, Terrain::wall_horizontal), 1));

    CHECK(hud.attempt_count() == 2);
    CHECK(hud.move_count() == 1);
    CHECK_FALSE(hud.last_move_succeeded());
}

TEST_CASE("last_move_succeeded tracks the most recent outcome") {
    Hud hud;
    hud.record_event(move_event(Direction::up, blocked({0, 0}, Terrain::wall_horizontal), 0));
    CHECK_FALSE(hud.last_move_succeeded());
    hud.record_event(move_event(Direction::down, moved_to({0, 0}, {0, 1}, Terrain::open), 1));
    CHECK(hud.last_move_succeeded());
}

TEST_CASE("the recent-move history is bounded regardless of session length") {
    Hud hud;
    for (int i = 0; i < 1000; ++i) {
        hud.record_event(move_event(Direction::right, moved_to({0, 0}, {1, 0}, Terrain::open),
                                    static_cast<std::uint64_t>(i)));
    }
    CHECK(hud.recent().size() == Hud::recent_capacity);
    CHECK(hud.move_count() == 1000);
    CHECK(hud.attempt_count() == 1000);
}

TEST_CASE("recording a move sets the latest-event message") {
    Hud hud;
    const MoveOutcome outcome = moved_to({0, 0}, {1, 0}, Terrain::water);
    hud.record_event(move_event(Direction::right, outcome, 0));
    CHECK(hud.message() == describe_move(outcome));
}

TEST_CASE("only a successful move records a recent entry carrying its direction") {
    Hud hud;
    // A blocked attempt increments attempts but records no history entry.
    hud.record_event(move_event(Direction::left, blocked({2, 2}, Terrain::wall_vertical), 0));
    CHECK(hud.attempt_count() == 1);
    CHECK(hud.recent().empty());

    // A successful move records exactly one direction-only entry.
    hud.record_event(move_event(Direction::right, moved_to({2, 2}, {3, 2}, Terrain::open), 1));
    REQUIRE(hud.recent().size() == 1);
    CHECK(hud.recent().back().direction == Direction::right);
}

TEST_CASE("set_message overrides the message without recording a move") {
    Hud hud;
    hud.set_message("Welcome.");
    CHECK(hud.message() == "Welcome.");
    CHECK(hud.attempt_count() == 0);
    CHECK(hud.move_count() == 0);
    CHECK(hud.recent().empty());
}

TEST_CASE("boundary, wall, and stamina blocks count attempts but add no history") {
    Hud hud;
    hud.record_event(move_event(Direction::up,
                                MoveOutcome{MoveResult::blocked_by_boundary, {0, 0}, {0, 0},
                                            Terrain::open, 0, 12, 12},
                                0));
    hud.record_event(move_event(Direction::left, blocked({0, 0}, Terrain::wall_vertical), 1));
    hud.record_event(move_event(Direction::right, stamina_blocked({0, 0}, Terrain::mountain, 4, 1),
                                2));

    CHECK(hud.attempt_count() == 3);
    CHECK(hud.move_count() == 0);
    CHECK(hud.recent().empty());
    CHECK_FALSE(hud.last_move_succeeded());
}

TEST_CASE("the recent history capacity counts successful moves only") {
    Hud hud;
    // Interleave a blocked attempt before every successful move: only the
    // successful moves consume history capacity, so the bound is on real moves.
    for (int i = 0; i < 1000; ++i) {
        hud.record_event(move_event(Direction::up, blocked({0, 0}, Terrain::wall_horizontal),
                                    static_cast<std::uint64_t>(2 * i)));
        hud.record_event(move_event(Direction::right, moved_to({0, 0}, {1, 0}, Terrain::open),
                                    static_cast<std::uint64_t>(2 * i + 1)));
    }
    CHECK(hud.recent().size() == Hud::recent_capacity);
    CHECK(hud.move_count() == 1000);
    CHECK(hud.attempt_count() == 2000);
    // Every stored entry is the successful right move.
    for (const RecentMove& entry : hud.recent()) {
        CHECK(entry.direction == Direction::right);
    }
}

TEST_CASE("a rest updates only the message and success flag, leaving counters and history") {
    Hud hud;
    hud.record_event(move_event(Direction::right, moved_to({0, 0}, {1, 0}, Terrain::open), 0));
    const std::size_t moves = hud.move_count();
    const std::size_t attempts = hud.attempt_count();
    const std::size_t history = hud.recent().size();

    hud.record_event(rest_event(4, 4, 1));
    CHECK(hud.move_count() == moves);
    CHECK(hud.attempt_count() == attempts);
    CHECK(hud.recent().size() == history);
    CHECK_FALSE(hud.last_move_succeeded());
    CHECK(hud.message() == "Rested and recovered 4 stamina.");

    hud.record_event(rest_event(12, 0, 2));
    CHECK(hud.move_count() == moves);
    CHECK(hud.attempt_count() == attempts);
    CHECK(hud.recent().size() == history);
    CHECK(hud.message() == "Stamina is already full.");
}

TEST_CASE("a stamina block is an attempt, not a move, and records no history") {
    Hud hud;
    hud.record_event(move_event(Direction::right, moved_to({0, 0}, {1, 0}, Terrain::open), 0));
    const MoveOutcome outcome = stamina_blocked({1, 0}, Terrain::mountain, 4, 1);
    hud.record_event(move_event(Direction::right, outcome, 1));

    // The block counts as an attempt but does not advance the move count.
    CHECK(hud.attempt_count() == 2);
    CHECK(hud.move_count() == 1);
    // The last move is not successful, so emphasis is off and no entry is added.
    CHECK_FALSE(hud.last_move_succeeded());
    REQUIRE(hud.recent().size() == 1);
    CHECK(hud.recent().back().direction == Direction::right);
    // The typed insufficient-stamina message is shown verbatim.
    CHECK(hud.message() == describe_move(outcome));
    CHECK(hud.message() == "Not enough stamina for mountain: need 4, have 1.");
}

}  // TEST_SUITE("console")
