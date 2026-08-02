#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

#include "coordinates.h"
#include "direction.h"
#include "game_state.h"
#include "map.h"
#include "terrain.h"
#include "visibility.h"

// Shared scripted-walk helper for tests that have to play a level rather than
// assert about one. It lives here because the core, console and acceptance
// suites all need the same walk, and three copies of one breadth-first search is
// three places for it to drift.
//
// This is test-only on purpose. The core deliberately owns no auto-explorer:
// deciding where to look next is the player's job, and nothing in the game needs
// to make that decision for them.

namespace nam::test {

// The nearest reachable cell the actor has never explored, or nullopt once the
// level is fully uncovered. Breadth-first from the actor over walkable cells, so
// the first hit is the cheapest place left to look and repeatedly walking there
// sweeps a level in close to the fewest moves possible.
[[nodiscard]] inline std::optional<Coordinates> nearest_unexplored(const GameState& state) {
    const Map& map = state.map();
    const int width = static_cast<int>(map.width());
    const int height = static_cast<int>(map.height());
    std::vector<char> seen(map.width() * map.height(), 0);
    const Coordinates start = state.actor_position();
    std::deque<Coordinates> queue{start};
    seen[static_cast<std::size_t>(start.y * width + start.x)] = 1;
    while (!queue.empty()) {
        const Coordinates cell = queue.front();
        queue.pop_front();
        if (state.visibility().at(cell) == CellVisibility::unexplored) return cell;
        for (const Direction direction :
             {Direction::up, Direction::down, Direction::left, Direction::right}) {
            const Coordinates next = cell + direction_delta(direction);
            if (next.x < 0 || next.y < 0 || next.x >= width || next.y >= height) continue;
            if (!is_walkable(map.terrain_at(next))) continue;
            char& mark = seen[static_cast<std::size_t>(next.y * width + next.x)];
            if (mark) continue;
            mark = 1;
            queue.push_back(next);
        }
    }
    return std::nullopt;
}

}  // namespace nam::test
