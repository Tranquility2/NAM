#include <doctest/doctest.h>

#include <string>

#include "app_state.h"
#include "coordinates.h"
#include "direction.h"
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

}  // namespace

TEST_SUITE("console") {

TEST_CASE("attempts always count while only successful moves advance the move count") {
    Hud hud;
    hud.record_move(Direction::right, moved_to({0, 0}, {1, 0}, Terrain::open));
    hud.record_move(Direction::up, blocked({1, 0}, Terrain::wall_horizontal));

    CHECK(hud.attempt_count() == 2);
    CHECK(hud.move_count() == 1);
    CHECK_FALSE(hud.last_move_succeeded());
}

TEST_CASE("last_move_succeeded tracks the most recent outcome") {
    Hud hud;
    hud.record_move(Direction::up, blocked({0, 0}, Terrain::wall_horizontal));
    CHECK_FALSE(hud.last_move_succeeded());
    hud.record_move(Direction::down, moved_to({0, 0}, {0, 1}, Terrain::open));
    CHECK(hud.last_move_succeeded());
}

TEST_CASE("the recent-move history is bounded regardless of session length") {
    Hud hud;
    for (int i = 0; i < 1000; ++i) {
        hud.record_move(Direction::right, moved_to({0, 0}, {1, 0}, Terrain::open));
    }
    CHECK(hud.recent().size() == Hud::recent_capacity);
    CHECK(hud.move_count() == 1000);
    CHECK(hud.attempt_count() == 1000);
}

TEST_CASE("recording a move sets the latest-event message") {
    Hud hud;
    const MoveOutcome outcome = moved_to({0, 0}, {1, 0}, Terrain::water);
    hud.record_move(Direction::right, outcome);
    CHECK(hud.message() == describe_move(outcome));
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
