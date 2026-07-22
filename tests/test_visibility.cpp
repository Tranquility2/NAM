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

TEST_CASE("centered base/hill/mountain spawns reveal exactly 25/49/81 cells") {
    // TASK-006 / TEST-003..005: an 11x11 map centered at (5,5) never clips radius
    // 4, so each terrain reveals exactly its full square.
    const GameState open_state(filled_map(11, 11, Terrain::open, Coordinates{5, 5}));
    CHECK(count_state(open_state.visibility(), CellVisibility::visible) == 25);
    CHECK(open_state.visibility_radius() == 2);

    const GameState fields_state(filled_map(11, 11, Terrain::fields, Coordinates{5, 5}));
    CHECK(count_state(fields_state.visibility(), CellVisibility::visible) == 25);
    CHECK(fields_state.visibility_radius() == 2);

    const GameState water_state(filled_map(11, 11, Terrain::water, Coordinates{5, 5}));
    CHECK(count_state(water_state.visibility(), CellVisibility::visible) == 25);
    CHECK(water_state.visibility_radius() == 2);

    const GameState hill_state(filled_map(11, 11, Terrain::hill, Coordinates{5, 5}));
    CHECK(count_state(hill_state.visibility(), CellVisibility::visible) == 49);
    CHECK(hill_state.visibility_radius() == 3);
    for (int y = 0; y < 11; ++y) {
        for (int x = 0; x < 11; ++x) {
            const Coordinates here{x, y};
            CHECK(hill_state.visibility().at(here) ==
                  expected_initial(here, Coordinates{5, 5}, 3));
        }
    }

    const GameState mountain_state(filled_map(11, 11, Terrain::mountain, Coordinates{5, 5}));
    CHECK(count_state(mountain_state.visibility(), CellVisibility::visible) == 81);
    CHECK(mountain_state.visibility_radius() == 4);
    for (int y = 0; y < 11; ++y) {
        for (int x = 0; x < 11; ++x) {
            const Coordinates here{x, y};
            CHECK(mountain_state.visibility().at(here) ==
                  expected_initial(here, Coordinates{5, 5}, 4));
        }
    }
}

TEST_CASE("hill and mountain spawns clip safely at corners and edges") {
    // TASK-007 / TEST-006: corner and edge spawns must clip the elevated square
    // to the exact in-bounds count without changing reveal arithmetic.

    // Hill in the top-left corner: radius 3 keeps x=0..3, y=0..3 -> 16 cells.
    const GameState hill_corner(filled_map(11, 11, Terrain::hill, Coordinates{0, 0}));
    CHECK(count_state(hill_corner.visibility(), CellVisibility::visible) == 16);
    for (int y = 0; y < 11; ++y) {
        for (int x = 0; x < 11; ++x) {
            const bool inside = x <= 3 && y <= 3;
            CHECK(hill_corner.visibility().at(Coordinates{x, y}) ==
                  (inside ? CellVisibility::visible : CellVisibility::unexplored));
        }
    }

    // Mountain in the top-left corner: radius 4 keeps x=0..4, y=0..4 -> 25 cells.
    const GameState mountain_corner(filled_map(11, 11, Terrain::mountain, Coordinates{0, 0}));
    CHECK(count_state(mountain_corner.visibility(), CellVisibility::visible) == 25);
    for (int y = 0; y < 11; ++y) {
        for (int x = 0; x < 11; ++x) {
            const bool inside = x <= 4 && y <= 4;
            CHECK(mountain_corner.visibility().at(Coordinates{x, y}) ==
                  (inside ? CellVisibility::visible : CellVisibility::unexplored));
        }
    }

    // Mountain on the left edge (0,5): x clips to 0..4 (5) but y stays 1..9 (9),
    // so the count is 5 * 9 = 45.
    const GameState mountain_edge(filled_map(11, 11, Terrain::mountain, Coordinates{0, 5}));
    CHECK(count_state(mountain_edge.visibility(), CellVisibility::visible) == 45);
    for (int y = 0; y < 11; ++y) {
        for (int x = 0; x < 11; ++x) {
            const bool inside = x <= 4 && y >= 1 && y <= 9;
            CHECK(mountain_edge.visibility().at(Coordinates{x, y}) ==
                  (inside ? CellVisibility::visible : CellVisibility::unexplored));
        }
    }
}

TEST_CASE("entering a hill expands visibility from radius 2 to a 7x7 square") {
    // TASK-008 / TEST-007: open spawn (4,5) sees radius 2; stepping right onto a
    // hill at (5,5) reveals the 7x7 square and every newly covered cell that was
    // unexplored becomes visible.
    std::vector<Terrain> cells(11 * 11, Terrain::open);
    cells[static_cast<std::size_t>(5) * 11 + 5] = Terrain::hill;  // (5,5)
    GameState state(Map(11, 11, std::move(cells), Coordinates{4, 5}));
    CHECK(state.visibility_radius() == 2);

    // Cells visible from the open spawn square around (4,5), radius 2.
    auto in_open_square = [](int x, int y) {
        return x >= 2 && x <= 6 && y >= 3 && y <= 7;
    };

    const GameEvent event = state.move(Direction::right);  // -> hill (5,5)
    CHECK(std::get<MoveAttemptedEvent>(event.data).outcome.result == MoveResult::moved);
    CHECK(state.actor_position() == Coordinates{5, 5});
    CHECK(state.visibility_radius() == 3);

    const VisibilityMap& visibility = state.visibility();
    CHECK(count_state(visibility, CellVisibility::visible) == 49);

    for (int y = 0; y < 11; ++y) {
        for (int x = 0; x < 11; ++x) {
            const Coordinates here{x, y};
            const bool now_visible = x >= 2 && x <= 8 && y >= 2 && y <= 8;
            if (now_visible) {
                // Every newly covered cell (outside the prior open square) and
                // every retained cell in the new square is currently visible.
                CHECK(visibility.at(here) == CellVisibility::visible);
            } else if (in_open_square(x, y)) {
                CHECK(visibility.at(here) == CellVisibility::remembered);
            } else {
                CHECK(visibility.at(here) == CellVisibility::unexplored);
            }
        }
    }
}

TEST_CASE("entering a mountain expands visibility from radius 3 to 4") {
    // TASK-009 / TEST-008: a hill spawn (4,5) sees radius 3; stepping right onto
    // a mountain at (5,5) grows the radius to 4 and the newly covered ring at
    // x=1 / x=9 / y=1 / y=9 becomes visible.
    std::vector<Terrain> cells(11 * 11, Terrain::hill);
    cells[static_cast<std::size_t>(5) * 11 + 5] = Terrain::mountain;  // (5,5)
    GameState state(Map(11, 11, std::move(cells), Coordinates{4, 5}));
    CHECK(state.visibility_radius() == 3);
    CHECK(count_state(state.visibility(), CellVisibility::visible) == 49);

    const GameEvent event = state.move(Direction::right);  // -> mountain (5,5)
    CHECK(std::get<MoveAttemptedEvent>(event.data).outcome.result == MoveResult::moved);
    CHECK(state.visibility_radius() == 4);

    const VisibilityMap& visibility = state.visibility();
    CHECK(count_state(visibility, CellVisibility::visible) == 81);
    // The newly covered outer ring of the radius-4 square (centered at (5,5)).
    CHECK(visibility.at(Coordinates{1, 5}) == CellVisibility::visible);
    CHECK(visibility.at(Coordinates{9, 5}) == CellVisibility::visible);
    CHECK(visibility.at(Coordinates{5, 1}) == CellVisibility::visible);
    CHECK(visibility.at(Coordinates{5, 9}) == CellVisibility::visible);
}

TEST_CASE("leaving a mountain shrinks visibility and keeps the outer area remembered") {
    // TASK-010 / TEST-009: a mountain spawn (5,5) sees radius 4; stepping right
    // onto open ground at (6,5) shrinks the radius to 2 and every cell that
    // leaves the new 5x5 square becomes remembered, never unexplored.
    std::vector<Terrain> cells(11 * 11, Terrain::open);
    cells[static_cast<std::size_t>(5) * 11 + 5] = Terrain::mountain;  // (5,5)
    GameState state(Map(11, 11, std::move(cells), Coordinates{5, 5}));
    CHECK(state.visibility_radius() == 4);
    CHECK(count_state(state.visibility(), CellVisibility::visible) == 81);

    const GameEvent event = state.move(Direction::right);  // -> open (6,5)
    CHECK(std::get<MoveAttemptedEvent>(event.data).outcome.result == MoveResult::moved);
    CHECK(state.actor_position() == Coordinates{6, 5});
    CHECK(state.visibility_radius() == 2);

    const VisibilityMap& visibility = state.visibility();
    CHECK(count_state(visibility, CellVisibility::visible) == 25);

    // Union of the old radius-4 square around (5,5) and the new radius-2 square
    // around (6,5); nothing revealed is ever lost back to unexplored.
    for (int y = 0; y < 11; ++y) {
        for (int x = 0; x < 11; ++x) {
            const Coordinates here{x, y};
            const bool new_visible = x >= 4 && x <= 8 && y >= 3 && y <= 7;
            const bool old_visible = x >= 1 && x <= 9 && y >= 1 && y <= 9;
            CellVisibility expected = CellVisibility::unexplored;
            if (new_visible) {
                expected = CellVisibility::visible;
            } else if (old_visible) {
                expected = CellVisibility::remembered;
            }
            CHECK(visibility.at(here) == expected);
        }
    }
    // No revealed cell dropped back to unexplored: the 81 previously visible
    // mountain cells are now 25 visible plus 56 remembered.
    CHECK(count_state(visibility, CellVisibility::remembered) == 81 - 25);
}

TEST_CASE("an initial mountain spawn uses radius 4 rather than the old fixed radius 2") {
    // TASK-011 / TEST-010: constructing directly on a mountain must reveal the
    // full radius-4 square at once (81 cells), never the old fixed 5x5 (25).
    const GameState state(filled_map(11, 11, Terrain::mountain, Coordinates{5, 5}));
    CHECK(state.visibility_radius() == 4);
    CHECK(count_state(state.visibility(), CellVisibility::visible) == 81);
    CHECK(count_state(state.visibility(), CellVisibility::visible) != 25);
    // A far cell reachable only at radius 4 is already visible at spawn.
    CHECK(state.visibility().at(Coordinates{1, 5}) == CellVisibility::visible);
    CHECK(state.visibility().at(Coordinates{9, 5}) == CellVisibility::visible);
}

TEST_CASE("failed moves apply no terrain radius on hill or mountain terrain") {
    // TASK-012 / TEST-011..012: boundary, wall, and insufficient-stamina blocks
    // must preserve every visibility cell and never reveal an elevated square.

    // Boundary: a mountain spawn in the corner cannot step left off the map.
    {
        GameState state(filled_map(11, 11, Terrain::mountain, Coordinates{0, 5}));
        const std::vector<CellVisibility> before = snapshot(state.visibility());
        const GameEvent event = state.move(Direction::left);
        CHECK(std::get<MoveAttemptedEvent>(event.data).outcome.result ==
              MoveResult::blocked_by_boundary);
        CHECK(snapshot(state.visibility()) == before);
    }

    // Wall: a mountain sits to the right, but a wall blocks the actor first, so
    // the mountain's larger radius is never applied.
    {
        std::vector<Terrain> cells(11 * 11, Terrain::open);
        cells[static_cast<std::size_t>(5) * 11 + 6] = Terrain::wall_vertical;  // (6,5)
        cells[static_cast<std::size_t>(5) * 11 + 7] = Terrain::mountain;       // (7,5)
        GameState state(Map(11, 11, std::move(cells), Coordinates{5, 5}));
        const std::vector<CellVisibility> before = snapshot(state.visibility());
        const GameEvent event = state.move(Direction::right);
        CHECK(std::get<MoveAttemptedEvent>(event.data).outcome.result ==
              MoveResult::blocked_by_terrain);
        CHECK(snapshot(state.visibility()) == before);
    }

    // Insufficient stamina: draining onto three mountains leaves zero stamina, so
    // a fourth mountain step is unaffordable. The far cell that its radius-4
    // square would reveal must remain hidden.
    {
        std::vector<Terrain> cells(11 * 3, Terrain::open);
        for (int x = 0; x < 11; ++x) {
            cells[static_cast<std::size_t>(0) * 11 + static_cast<std::size_t>(x)] =
                Terrain::wall_horizontal;
            cells[static_cast<std::size_t>(2) * 11 + static_cast<std::size_t>(x)] =
                Terrain::wall_horizontal;
        }
        // Middle row: open spawn at x=0, mountains at x=1..4, a distinctive water
        // cell at x=8 (reachable only from the fourth mountain at x=4, radius 4).
        for (int x = 1; x <= 4; ++x) {
            cells[static_cast<std::size_t>(1) * 11 + static_cast<std::size_t>(x)] =
                Terrain::mountain;
        }
        cells[static_cast<std::size_t>(1) * 11 + 8] = Terrain::water;
        GameState state(Map(11, 3, std::move(cells), Coordinates{0, 1}));

        CHECK(state.move(Direction::right).sequence == 0);  // 12 -> 8, onto (1,1)
        (void)state.move(Direction::right);                 // 8 -> 4,  onto (2,1)
        (void)state.move(Direction::right);                 // 4 -> 0,  onto (3,1)
        CHECK(state.actor_position() == Coordinates{3, 1});
        CHECK(state.stamina() == 0);
        // The water at x=8 is outside radius 4 of the occupied mountain at x=3.
        CHECK(state.visibility().at(Coordinates{8, 1}) == CellVisibility::unexplored);

        const std::vector<CellVisibility> before = snapshot(state.visibility());
        const GameEvent blocked = state.move(Direction::right);  // need 4, have 0
        CHECK(std::get<MoveAttemptedEvent>(blocked.data).outcome.result ==
              MoveResult::blocked_by_stamina);
        CHECK(state.actor_position() == Coordinates{3, 1});
        CHECK(snapshot(state.visibility()) == before);
        // The distant water stays hidden until the actor can afford the mountain.
        CHECK(state.visibility().at(Coordinates{8, 1}) == CellVisibility::unexplored);
    }
}

TEST_CASE("peek and rest never change visibility on elevated terrain") {
    // TASK-013 / TEST-013: repeated peeks and a rest are visibility-pure even on
    // a mountain, where a premature reveal would leak the larger radius.
    GameState state(filled_map(11, 11, Terrain::mountain, Coordinates{5, 5}));
    const std::vector<CellVisibility> before = snapshot(state.visibility());

    for (int i = 0; i < 5; ++i) {
        (void)state.peek(Direction::up);
        (void)state.peek(Direction::right);
        (void)state.peek(Direction::down);
        (void)state.peek(Direction::left);
    }
    CHECK(snapshot(state.visibility()) == before);

    const GameEvent rested = state.rest();
    CHECK(std::holds_alternative<RestedEvent>(rested.data));
    CHECK(snapshot(state.visibility()) == before);
}

}  // TEST_SUITE("game")