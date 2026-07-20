#include <doctest/doctest.h>

#include <cstdint>
#include <string>

#include "app_state.h"
#include "coordinates.h"
#include "direction.h"
#include "game_event.h"
#include "messages.h"
#include "move_outcome.h"

using namespace nam::console;

namespace {

MoveOutcome moved_to(Coordinates from, Coordinates to, Terrain terrain) {
    return MoveOutcome{MoveResult::moved, from, to, terrain};
}

MoveOutcome blocked(Coordinates at, Terrain terrain) {
    return MoveOutcome{MoveResult::blocked_by_terrain, at, at, terrain};
}

// Wrap a direction and outcome into the movement event the HUD now consumes. The
// sequence is immaterial to HUD behaviour (it is not displayed yet), so a
// running counter keeps the events well-formed without the tests depending on it.
GameEvent move_event(Direction direction, const MoveOutcome& outcome, std::uint64_t sequence = 0) {
    return GameEvent{sequence, MoveAttemptedEvent{direction, outcome}};
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

TEST_CASE("the recorded recent entry carries the event's direction and result") {
    Hud hud;
    hud.record_event(move_event(Direction::left, blocked({2, 2}, Terrain::wall_vertical), 0));
    REQUIRE(hud.recent().size() == 1);
    CHECK(hud.recent().back().direction == Direction::left);
    CHECK(hud.recent().back().result == MoveResult::blocked_by_terrain);
}

TEST_CASE("set_message overrides the message without recording a move") {
    Hud hud;
    hud.set_message("Welcome.");
    CHECK(hud.message() == "Welcome.");
    CHECK(hud.attempt_count() == 0);
    CHECK(hud.move_count() == 0);
    CHECK(hud.recent().empty());
}

}  // TEST_SUITE("console")
