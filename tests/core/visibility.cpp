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

TEST_SUITE("visibility") {

TEST_CASE("a centered spawn reveals exactly the clipped 7x7 square") {
    // TASK-006 / TEST-001: spawn (5,5) on an 11x11 map reveals x=2..8, y=2..8.
    const GameState state(filled_map(11, 11, Terrain::open, Coordinates{5, 5}));
    const VisibilityMap& visibility = state.visibility();

    CHECK(visibility.width() == 11);
    CHECK(visibility.height() == 11);
    CHECK(count_state(visibility, CellVisibility::visible) == 49);
    CHECK(count_state(visibility, CellVisibility::remembered) == 0);

    for (int y = 0; y < 11; ++y) {
        for (int x = 0; x < 11; ++x) {
            const Coordinates here{x, y};
            CHECK(visibility.at(here) == expected_initial(here, Coordinates{5, 5}, 3));
        }
    }
}

TEST_CASE("a corner spawn clips safely to a 4x4 square") {
    // TASK-007 / TEST-002: spawn (0,0) reveals exactly the sixteen cells x=0..3,
    // y=0..3 and never indexes outside the map.
    const GameState state(filled_map(11, 11, Terrain::open, Coordinates{0, 0}));
    const VisibilityMap& visibility = state.visibility();

    CHECK(count_state(visibility, CellVisibility::visible) == 16);
    for (int y = 0; y < 11; ++y) {
        for (int x = 0; x < 11; ++x) {
            const Coordinates here{x, y};
            const bool inside = x <= 3 && y <= 3;
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
    // TASK-008 / TEST-003: moving (5,5) -> (6,5) reveals x=3..9, y=2..8 and
    // leaves the outgoing x=2, y=2..8 strip remembered.
    GameState state(filled_map(11, 11, Terrain::open, Coordinates{5, 5}));
    const GameEvent event = state.move(Direction::right);
    CHECK(state.actor_position() == Coordinates{6, 5});
    (void)event;

    const VisibilityMap& visibility = state.visibility();
    for (int y = 0; y < 11; ++y) {
        for (int x = 0; x < 11; ++x) {
            const Coordinates here{x, y};
            const bool visible = x >= 3 && x <= 9 && y >= 2 && y <= 8;
            const bool remembered = x == 2 && y >= 2 && y <= 8;
            CellVisibility expected = CellVisibility::unexplored;
            if (visible) {
                expected = CellVisibility::visible;
            } else if (remembered) {
                expected = CellVisibility::remembered;
            }
            CHECK(visibility.at(here) == expected);
        }
    }

    CHECK(count_state(visibility, CellVisibility::visible) == 49);
    CHECK(count_state(visibility, CellVisibility::remembered) == 7);
}

TEST_CASE("returning toward remembered cells makes them visible again") {
    // TASK-009 / TEST-004: after moving away and back, remembered cells become
    // visible again and cells leaving the opposite edge become remembered.
    GameState state(filled_map(15, 15, Terrain::open, Coordinates{7, 7}));
    const GameEvent right = state.move(Direction::right);  // -> (8,7)
    (void)right;
    const VisibilityMap& visibility = state.visibility();

    // The x=4 column left the square when moving right; it is remembered.
    CHECK(visibility.at(Coordinates{4, 7}) == CellVisibility::remembered);
    // The x=11 column just entered the square; it is visible.
    CHECK(visibility.at(Coordinates{11, 7}) == CellVisibility::visible);

    const GameEvent left = state.move(Direction::left);  // back to (7,7)
    (void)left;
    CHECK(state.actor_position() == Coordinates{7, 7});
    // The previously remembered x=4 column is currently visible again.
    CHECK(visibility.at(Coordinates{4, 7}) == CellVisibility::visible);
    // The x=11 column left the opposite edge and is now remembered, not lost.
    CHECK(visibility.at(Coordinates{11, 7}) == CellVisibility::remembered);
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
    // TASK-010 / TEST-006: a cliff to the actor's right blocks movement without
    // touching visibility.
    std::vector<Terrain> cells(7 * 7, Terrain::open);
    cells[static_cast<std::size_t>(3) * 7 + 4] = Terrain::cliff;  // (4,3)
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

TEST_CASE("every terrain reveals exactly its own sight square from a centered spawn") {
    // A 21x21 map centered at (10,10) never clips radius 7, so each terrain
    // reveals exactly the square its sight radius describes. These counts are
    // the terrain contract restated in cells: forest blinds, water and elevation
    // open up.
    struct Case {
        Terrain terrain;
        int radius;
        std::size_t visible;
    };
    const Case cases[] = {
        {Terrain::forest, 1, 9},
        {Terrain::open, 3, 49},
        {Terrain::fields, 3, 49},
        {Terrain::shallow_water, 4, 81},
        {Terrain::hill, 5, 121},
        {Terrain::mountain, 7, 225},
    };

    for (const Case& scenario : cases) {
        const GameState state(filled_map(21, 21, scenario.terrain, Coordinates{10, 10}));
        CHECK(state.visibility_radius() == scenario.radius);
        CHECK(count_state(state.visibility(), CellVisibility::visible) == scenario.visible);
        for (int y = 0; y < 21; ++y) {
            for (int x = 0; x < 21; ++x) {
                const Coordinates here{x, y};
                CHECK(state.visibility().at(here) ==
                      expected_initial(here, Coordinates{10, 10}, scenario.radius));
            }
        }
    }
}

TEST_CASE("forest sees strictly less than open ground and mountain strictly more") {
    // The ordering matters more than the exact numbers: the whole design rests on
    // terrain being rankable by what it shows you.
    CHECK(visibility_radius_of(Terrain::forest) < visibility_radius_of(Terrain::open));
    CHECK(visibility_radius_of(Terrain::open) == visibility_radius_of(Terrain::fields));
    CHECK(visibility_radius_of(Terrain::fields) < visibility_radius_of(Terrain::shallow_water));
    CHECK(visibility_radius_of(Terrain::shallow_water) < visibility_radius_of(Terrain::hill));
    CHECK(visibility_radius_of(Terrain::hill) < visibility_radius_of(Terrain::mountain));
}

TEST_CASE("elevated spawns clip safely at corners and edges") {
    // Corner and edge spawns must clip the elevated square to the exact in-bounds
    // count without changing reveal arithmetic.

    // Hill in the top-left corner: radius 5 keeps x=0..5, y=0..5 -> 36 cells.
    const GameState hill_corner(filled_map(21, 21, Terrain::hill, Coordinates{0, 0}));
    CHECK(count_state(hill_corner.visibility(), CellVisibility::visible) == 36);
    for (int y = 0; y < 21; ++y) {
        for (int x = 0; x < 21; ++x) {
            const bool inside = x <= 5 && y <= 5;
            CHECK(hill_corner.visibility().at(Coordinates{x, y}) ==
                  (inside ? CellVisibility::visible : CellVisibility::unexplored));
        }
    }

    // Mountain in the top-left corner: radius 7 keeps x=0..7, y=0..7 -> 64 cells.
    const GameState mountain_corner(filled_map(21, 21, Terrain::mountain, Coordinates{0, 0}));
    CHECK(count_state(mountain_corner.visibility(), CellVisibility::visible) == 64);
    for (int y = 0; y < 21; ++y) {
        for (int x = 0; x < 21; ++x) {
            const bool inside = x <= 7 && y <= 7;
            CHECK(mountain_corner.visibility().at(Coordinates{x, y}) ==
                  (inside ? CellVisibility::visible : CellVisibility::unexplored));
        }
    }

    // Mountain on the left edge (0,10): x clips to 0..7 (8) but y stays 3..17
    // (15), so the count is 8 * 15 = 120.
    const GameState mountain_edge(filled_map(21, 21, Terrain::mountain, Coordinates{0, 10}));
    CHECK(count_state(mountain_edge.visibility(), CellVisibility::visible) == 120);
    for (int y = 0; y < 21; ++y) {
        for (int x = 0; x < 21; ++x) {
            const bool inside = x <= 7 && y >= 3 && y <= 17;
            CHECK(mountain_edge.visibility().at(Coordinates{x, y}) ==
                  (inside ? CellVisibility::visible : CellVisibility::unexplored));
        }
    }
}

TEST_CASE("stepping out of forest into the open widens sight from 3x3 to 7x7") {
    // Forest is the only terrain that costs the player sight, so leaving it is
    // the smallest complete example of the core decision.
    std::vector<Terrain> cells(21 * 21, Terrain::open);
    cells[static_cast<std::size_t>(10) * 21 + 9] = Terrain::forest;  // (9,10)
    GameState state(Map(21, 21, std::move(cells), Coordinates{9, 10}));
    CHECK(state.visibility_radius() == 1);
    CHECK(count_state(state.visibility(), CellVisibility::visible) == 9);

    const GameEvent event = state.move(Direction::right);  // -> open (10,10)
    CHECK(std::get<MoveAttemptedEvent>(event.data).outcome.result == MoveResult::moved);
    CHECK(state.visibility_radius() == 3);
    CHECK(count_state(state.visibility(), CellVisibility::visible) == 49);
}

TEST_CASE("entering a hill expands visibility from radius 3 to an 11x11 square") {
    // Open spawn (9,10) sees radius 3; stepping right onto a hill at (10,10)
    // reveals the 11x11 square and every newly covered cell that was unexplored
    // becomes visible.
    std::vector<Terrain> cells(21 * 21, Terrain::open);
    cells[static_cast<std::size_t>(10) * 21 + 10] = Terrain::hill;  // (10,10)
    GameState state(Map(21, 21, std::move(cells), Coordinates{9, 10}));
    CHECK(state.visibility_radius() == 3);

    // Cells visible from the open spawn square around (9,10), radius 3.
    auto in_open_square = [](int x, int y) {
        return x >= 6 && x <= 12 && y >= 7 && y <= 13;
    };

    const GameEvent event = state.move(Direction::right);  // -> hill (10,10)
    CHECK(std::get<MoveAttemptedEvent>(event.data).outcome.result == MoveResult::moved);
    CHECK(state.actor_position() == Coordinates{10, 10});
    CHECK(state.visibility_radius() == 5);

    const VisibilityMap& visibility = state.visibility();
    CHECK(count_state(visibility, CellVisibility::visible) == 121);

    for (int y = 0; y < 21; ++y) {
        for (int x = 0; x < 21; ++x) {
            const Coordinates here{x, y};
            const bool now_visible = x >= 5 && x <= 15 && y >= 5 && y <= 15;
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

TEST_CASE("entering a mountain expands visibility from radius 5 to 7") {
    // A hill spawn (9,10) sees radius 5; stepping right onto a mountain at
    // (10,10) grows the radius to 7 and the newly covered outer ring becomes
    // visible.
    std::vector<Terrain> cells(21 * 21, Terrain::hill);
    cells[static_cast<std::size_t>(10) * 21 + 10] = Terrain::mountain;  // (10,10)
    GameState state(Map(21, 21, std::move(cells), Coordinates{9, 10}));
    CHECK(state.visibility_radius() == 5);
    CHECK(count_state(state.visibility(), CellVisibility::visible) == 121);

    const GameEvent event = state.move(Direction::right);  // -> mountain (10,10)
    CHECK(std::get<MoveAttemptedEvent>(event.data).outcome.result == MoveResult::moved);
    CHECK(state.visibility_radius() == 7);

    const VisibilityMap& visibility = state.visibility();
    CHECK(count_state(visibility, CellVisibility::visible) == 225);
    // The newly covered outer ring of the radius-7 square (centered at (10,10)).
    CHECK(visibility.at(Coordinates{3, 10}) == CellVisibility::visible);
    CHECK(visibility.at(Coordinates{17, 10}) == CellVisibility::visible);
    CHECK(visibility.at(Coordinates{10, 3}) == CellVisibility::visible);
    CHECK(visibility.at(Coordinates{10, 17}) == CellVisibility::visible);
}

TEST_CASE("leaving a mountain shrinks visibility and keeps the outer area remembered") {
    // A mountain spawn (10,10) sees radius 7; stepping right onto open ground at
    // (11,10) shrinks the radius to 3 and every cell that leaves the new square
    // becomes remembered, never unexplored.
    std::vector<Terrain> cells(21 * 21, Terrain::open);
    cells[static_cast<std::size_t>(10) * 21 + 10] = Terrain::mountain;  // (10,10)
    GameState state(Map(21, 21, std::move(cells), Coordinates{10, 10}));
    CHECK(state.visibility_radius() == 7);
    CHECK(count_state(state.visibility(), CellVisibility::visible) == 225);

    const GameEvent event = state.move(Direction::right);  // -> open (11,10)
    CHECK(std::get<MoveAttemptedEvent>(event.data).outcome.result == MoveResult::moved);
    CHECK(state.actor_position() == Coordinates{11, 10});
    CHECK(state.visibility_radius() == 3);

    const VisibilityMap& visibility = state.visibility();
    CHECK(count_state(visibility, CellVisibility::visible) == 49);

    // Union of the old radius-7 square around (10,10) and the new radius-3 square
    // around (11,10); nothing revealed is ever lost back to unexplored.
    for (int y = 0; y < 21; ++y) {
        for (int x = 0; x < 21; ++x) {
            const Coordinates here{x, y};
            const bool new_visible = x >= 8 && x <= 14 && y >= 7 && y <= 13;
            const bool old_visible = x >= 3 && x <= 17 && y >= 3 && y <= 17;
            CellVisibility expected = CellVisibility::unexplored;
            if (new_visible) {
                expected = CellVisibility::visible;
            } else if (old_visible) {
                expected = CellVisibility::remembered;
            }
            CHECK(visibility.at(here) == expected);
        }
    }
    // No revealed cell dropped back to unexplored: the 225 previously visible
    // mountain cells are now 49 visible plus the rest remembered.
    CHECK(count_state(visibility, CellVisibility::remembered) == 225 - 49);
}

TEST_CASE("an initial mountain spawn uses its full radius rather than the base radius") {
    // Constructing directly on a mountain must reveal the full radius-7 square at
    // once (225 cells), never the base open square (49).
    const GameState state(filled_map(21, 21, Terrain::mountain, Coordinates{10, 10}));
    CHECK(state.visibility_radius() == 7);
    CHECK(count_state(state.visibility(), CellVisibility::visible) == 225);
    CHECK(count_state(state.visibility(), CellVisibility::visible) != 49);
    // A far cell reachable only at radius 7 is already visible at spawn.
    CHECK(state.visibility().at(Coordinates{3, 10}) == CellVisibility::visible);
    CHECK(state.visibility().at(Coordinates{17, 10}) == CellVisibility::visible);
}

TEST_CASE("failed moves apply no terrain radius on hill or mountain terrain") {
    // Boundary and barrier blocks must preserve every visibility cell and never
    // reveal an elevated square.

    // Boundary: a mountain spawn on the edge cannot step left off the map.
    {
        GameState state(filled_map(21, 21, Terrain::mountain, Coordinates{0, 10}));
        const std::vector<CellVisibility> before = snapshot(state.visibility());
        const GameEvent event = state.move(Direction::left);
        CHECK(std::get<MoveAttemptedEvent>(event.data).outcome.result ==
              MoveResult::blocked_by_boundary);
        CHECK(snapshot(state.visibility()) == before);
    }

    // Cliff: a mountain sits to the right, but a cliff blocks the actor first, so
    // the mountain's larger radius is never applied.
    {
        std::vector<Terrain> cells(21 * 21, Terrain::open);
        cells[static_cast<std::size_t>(10) * 21 + 11] = Terrain::cliff;     // (11,10)
        cells[static_cast<std::size_t>(10) * 21 + 12] = Terrain::mountain;  // (12,10)
        GameState state(Map(21, 21, std::move(cells), Coordinates{10, 10}));
        const std::vector<CellVisibility> before = snapshot(state.visibility());
        const GameEvent event = state.move(Direction::right);
        CHECK(std::get<MoveAttemptedEvent>(event.data).outcome.result ==
              MoveResult::blocked_by_terrain);
        CHECK(snapshot(state.visibility()) == before);
    }

    // Deep water blocks exactly like a cliff, and just as silently.
    {
        std::vector<Terrain> cells(21 * 21, Terrain::open);
        cells[static_cast<std::size_t>(10) * 21 + 11] = Terrain::deep_water;  // (11,10)
        GameState state(Map(21, 21, std::move(cells), Coordinates{10, 10}));
        const std::vector<CellVisibility> before = snapshot(state.visibility());
        const GameEvent event = state.move(Direction::right);
        CHECK(std::get<MoveAttemptedEvent>(event.data).outcome.result ==
              MoveResult::blocked_by_terrain);
        CHECK(state.actor_position() == Coordinates{10, 10});
        CHECK(snapshot(state.visibility()) == before);
    }

    // A blocked move applies no terrain radius even after a long elevated march.
    // Walking a mountain ridge up to a sealing cliff must leave the far cell that
    // a reveal from beyond the cliff would uncover hidden.
    {
        constexpr int kWidth = 25;
        std::vector<Terrain> cells(kWidth * 3, Terrain::open);
        for (int x = 0; x < kWidth; ++x) {
            cells[static_cast<std::size_t>(x)] = Terrain::cliff;
            cells[static_cast<std::size_t>(2) * kWidth + static_cast<std::size_t>(x)] =
                Terrain::cliff;
        }
        // Middle row: open spawn at x=0, mountains at x=1..4, a sealing cliff at
        // x=5, and a distinctive shallow-water cell at x=23 that lies outside
        // radius 7 of every reachable cell.
        for (int x = 1; x <= 4; ++x) {
            cells[static_cast<std::size_t>(kWidth) + static_cast<std::size_t>(x)] =
                Terrain::mountain;
        }
        cells[static_cast<std::size_t>(kWidth) + 5] = Terrain::cliff;
        cells[static_cast<std::size_t>(kWidth) + 23] = Terrain::shallow_water;
        GameState state(Map(kWidth, 3, std::move(cells), Coordinates{0, 1}));

        CHECK(state.move(Direction::right).sequence == 0);  // onto (1,1)
        static_cast<void>(state.move(Direction::right));    // onto (2,1)
        static_cast<void>(state.move(Direction::right));    // onto (3,1)
        static_cast<void>(state.move(Direction::right));    // onto (4,1)
        CHECK(state.actor_position() == Coordinates{4, 1});
        // The water at x=23 is outside radius 7 of the occupied mountain at x=4.
        CHECK(state.visibility().at(Coordinates{23, 1}) == CellVisibility::unexplored);

        const std::vector<CellVisibility> before = snapshot(state.visibility());
        const GameEvent blocked = state.move(Direction::right);  // the cliff refuses
        CHECK(std::get<MoveAttemptedEvent>(blocked.data).outcome.result ==
              MoveResult::blocked_by_terrain);
        CHECK(state.actor_position() == Coordinates{4, 1});
        CHECK(snapshot(state.visibility()) == before);
        // The distant water stays hidden behind the cliff.
        CHECK(state.visibility().at(Coordinates{23, 1}) == CellVisibility::unexplored);
    }
}

}  // TEST_SUITE("visibility")
