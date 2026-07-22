#include <doctest/doctest.h>

#include <cstddef>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "coordinates.h"
#include "direction.h"
#include "game_event.h"
#include "game_state.h"
#include "map.h"
#include "move_outcome.h"
#include "terrain.h"
#include "visibility.h"

namespace {

// Build a rectangular map of the given terrain with a chosen spawn, without a
// parser round-trip so tests control geometry exactly.
Map filled_map(std::size_t width, std::size_t height, Terrain terrain, Coordinates spawn) {
    return Map(width, height, std::vector<Terrain>(width * height, terrain), spawn);
}

// Snapshot every visibility cell in row-major order for change comparisons.
std::vector<CellVisibility> snapshot(const VisibilityMap& visibility) {
    std::vector<CellVisibility> cells;
    cells.reserve(visibility.width() * visibility.height());
    for (std::size_t y = 0; y < visibility.height(); ++y) {
        for (std::size_t x = 0; x < visibility.width(); ++x) {
            cells.push_back(visibility.at(Coordinates{static_cast<int>(x), static_cast<int>(y)}));
        }
    }
    return cells;
}

// The expected state of a cell for a single sight square centered at `center`
// with `radius`, assuming no prior exploration: visible inside the square,
// otherwise unexplored.
CellVisibility expected_initial(Coordinates here, Coordinates center, int radius) {
    const bool inside = here.x >= center.x - radius && here.x <= center.x + radius &&
                        here.y >= center.y - radius && here.y <= center.y + radius;
    return inside ? CellVisibility::visible : CellVisibility::unexplored;
}

std::size_t count_state(const VisibilityMap& visibility, CellVisibility state) {
    std::size_t total = 0;
    for (const CellVisibility cell : snapshot(visibility)) {
        if (cell == state) {
            ++total;
        }
    }
    return total;
}

}  // namespace

TEST_SUITE("game") {

TEST_CASE("a centered spawn reveals exactly the clipped 5x5 square") {
    // TASK-006 / TEST-001: spawn (3,3) on a 7x7 map reveals x=1..5, y=1..5.
    const GameState state(filled_map(7, 7, Terrain::open, Coordinates{3, 3}));
    const VisibilityMap& visibility = state.visibility();

    CHECK(visibility.width() == 7);
    CHECK(visibility.height() == 7);
    CHECK(count_state(visibility, CellVisibility::visible) == 25);
    CHECK(count_state(visibility, CellVisibility::remembered) == 0);

    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
            const Coordinates here{x, y};
            CHECK(visibility.at(here) == expected_initial(here, Coordinates{3, 3}, 2));
        }
    }
}

TEST_CASE("a corner spawn clips safely to a 3x3 square") {
    // TASK-007 / TEST-002: spawn (0,0) reveals exactly the nine cells x=0..2,
    // y=0..2 and never indexes outside the map.
    const GameState state(filled_map(7, 7, Terrain::open, Coordinates{0, 0}));
    const VisibilityMap& visibility = state.visibility();

    CHECK(count_state(visibility, CellVisibility::visible) == 9);
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
            const Coordinates here{x, y};
            const bool inside = x <= 2 && y <= 2;
            CHECK(visibility.at(here) ==
                  (inside ? CellVisibility::visible : CellVisibility::unexplored));
        }
    }
}

TEST_CASE("reveal_square clips a radius larger than the map to every cell") {
    // REQ-006: with a radius that exceeds both dimensions the upper-bound radius
    // capping must saturate at the far edge on each axis and the inclusive-break
    // loops must visit every cell without wrapping or indexing out of bounds.
    VisibilityMap visibility(4, 3);
    visibility.reveal_square(Coordinates{3, 2}, 1000);

    CHECK(count_state(visibility, CellVisibility::visible) == 12);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 4; ++x) {
            CHECK(visibility.at(Coordinates{x, y}) == CellVisibility::visible);
        }
    }
}

TEST_CASE("reveal_square reveals a single cell on a 1x1 map") {
    // REQ-006: the smallest map exercises the guarded lower bound (center == 0)
    // and the capped-at-zero upper bound simultaneously.
    VisibilityMap visibility(1, 1);
    visibility.reveal_square(Coordinates{0, 0}, 2);

    CHECK(count_state(visibility, CellVisibility::visible) == 1);
    CHECK(visibility.at(Coordinates{0, 0}) == CellVisibility::visible);
}

TEST_CASE("a successful move produces a new visible square and remembered strip") {
    // TASK-008 / TEST-003: moving (3,3) -> (4,3) reveals x=2..6, y=1..5 and
    // leaves the outgoing x=1, y=1..5 strip remembered.
    GameState state(filled_map(7, 7, Terrain::open, Coordinates{3, 3}));
    const GameEvent event = state.move(Direction::right);
    CHECK(state.actor_position() == Coordinates{4, 3});
    (void)event;

    const VisibilityMap& visibility = state.visibility();
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
            const Coordinates here{x, y};
            const bool visible = x >= 2 && x <= 6 && y >= 1 && y <= 5;
            const bool remembered = x == 1 && y >= 1 && y <= 5;
            CellVisibility expected = CellVisibility::unexplored;
            if (visible) {
                expected = CellVisibility::visible;
            } else if (remembered) {
                expected = CellVisibility::remembered;
            }
            CHECK(visibility.at(here) == expected);
        }
    }

    CHECK(count_state(visibility, CellVisibility::visible) == 25);
    CHECK(count_state(visibility, CellVisibility::remembered) == 5);
}

TEST_CASE("returning toward remembered cells makes them visible again") {
    // TASK-009 / TEST-004: after moving away and back, remembered cells become
    // visible again and cells leaving the opposite edge become remembered.
    GameState state(filled_map(9, 9, Terrain::open, Coordinates{4, 4}));
    const GameEvent right = state.move(Direction::right);  // -> (5,4)
    (void)right;
    const VisibilityMap& visibility = state.visibility();

    // x=2 column (y=2..6) left the square when moving right; it is remembered.
    CHECK(visibility.at(Coordinates{2, 4}) == CellVisibility::remembered);
    // x=7 column just entered the square; it is visible.
    CHECK(visibility.at(Coordinates{7, 4}) == CellVisibility::visible);

    const GameEvent left = state.move(Direction::left);  // back to (4,4)
    (void)left;
    CHECK(state.actor_position() == Coordinates{4, 4});
    // The previously remembered x=2 column is currently visible again.
    CHECK(visibility.at(Coordinates{2, 4}) == CellVisibility::visible);
    // The x=7 column left the opposite edge and is now remembered, not lost.
    CHECK(visibility.at(Coordinates{7, 4}) == CellVisibility::remembered);
}

TEST_CASE("blocked boundary moves leave visibility unchanged") {
    // TASK-010 / TEST-005.
    GameState state(filled_map(7, 7, Terrain::open, Coordinates{0, 0}));
    const std::vector<CellVisibility> before = snapshot(state.visibility());

    const GameEvent event = state.move(Direction::left);  // into the boundary
    (void)event;
    CHECK(state.actor_position() == Coordinates{0, 0});
    CHECK(snapshot(state.visibility()) == before);
}

TEST_CASE("blocked terrain moves leave visibility unchanged") {
    // TASK-010 / TEST-006: a wall to the actor's right blocks movement without
    // touching visibility.
    std::vector<Terrain> cells(7 * 7, Terrain::open);
    cells[static_cast<std::size_t>(3) * 7 + 4] = Terrain::wall_vertical;  // (4,3)
    GameState state(Map(7, 7, std::move(cells), Coordinates{3, 3}));
    const std::vector<CellVisibility> before = snapshot(state.visibility());

    const GameEvent event = state.move(Direction::right);
    CHECK(std::get<MoveAttemptedEvent>(event.data).outcome.result ==
          MoveResult::blocked_by_terrain);
    CHECK(state.actor_position() == Coordinates{3, 3});
    CHECK(snapshot(state.visibility()) == before);
}

TEST_CASE("repeated peek leaves visibility unchanged") {
    // TASK-010 / TEST-007.
    GameState state(filled_map(7, 7, Terrain::open, Coordinates{3, 3}));
    const std::vector<CellVisibility> before = snapshot(state.visibility());

    for (int i = 0; i < 5; ++i) {
        (void)state.peek(Direction::up);
        (void)state.peek(Direction::right);
    }
    CHECK(snapshot(state.visibility()) == before);
}

TEST_CASE("identical maps and move scripts produce identical visibility") {
    // TASK-011 / TEST-008.
    const std::vector<Direction> script{
        Direction::right, Direction::right, Direction::down, Direction::left,
        Direction::up,    Direction::up,    Direction::left, Direction::down};

    GameState a(filled_map(9, 9, Terrain::open, Coordinates{4, 4}));
    GameState b(filled_map(9, 9, Terrain::open, Coordinates{4, 4}));

    CHECK(snapshot(a.visibility()) == snapshot(b.visibility()));
    for (const Direction direction : script) {
        const GameEvent ea = a.move(direction);
        const GameEvent eb = b.move(direction);
        CHECK(ea.sequence == eb.sequence);
        const MoveAttemptedEvent& pa = std::get<MoveAttemptedEvent>(ea.data);
        const MoveAttemptedEvent& pb = std::get<MoveAttemptedEvent>(eb.data);
        CHECK(pa.direction == pb.direction);
        CHECK(pa.outcome.result == pb.outcome.result);
        CHECK(pa.outcome.to == pb.outcome.to);
        CHECK(a.actor_position() == b.actor_position());
        CHECK(snapshot(a.visibility()) == snapshot(b.visibility()));
    }
}

TEST_CASE("visibility updates never mutate full-map serialization") {
    // TASK-012 / TEST-009: Map::to_string() is invariant and GameState::render()
    // changes only by the actor overlay moving, exactly as before fog existed.
    GameState state(filled_map(7, 7, Terrain::open, Coordinates{3, 3}));
    const Map& map = state.map();

    const std::string map_before = map.to_string();
    const std::string render_before = state.render('O');
    CHECK(render_before == map.to_string(Coordinates{3, 3}, 'O'));

    const GameEvent event = state.move(Direction::right);
    (void)event;

    // The underlying terrain serialization is unchanged by exploration.
    CHECK(map.to_string() == map_before);
    // render() differs only because the actor overlay moved to the new cell.
    const std::string render_after = state.render('O');
    CHECK(render_after != render_before);
    CHECK(render_after == map.to_string(Coordinates{4, 3}, 'O'));
}

}  // TEST_SUITE("game")
