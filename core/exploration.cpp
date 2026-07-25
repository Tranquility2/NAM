#include "exploration.h"

#include <array>
#include <cstddef>
#include <vector>

#include "coordinates.h"
#include "terrain.h"

namespace {

// The four cardinal neighbour offsets used to define reachability, matching the
// walkable-move relation used everywhere else in the core.
constexpr std::array<Coordinates, 4> kCardinalOffsets{
    Coordinates{0, -1}, Coordinates{0, 1}, Coordinates{-1, 0}, Coordinates{1, 0}};

// Breadth-first mark of every walkable cell reachable from spawn over cardinal
// walkable steps. The returned mask is row-major (index = y * width + x) and
// includes spawn.
[[nodiscard]] std::vector<bool> reachable_mask(const Map& map) {
    const int width = static_cast<int>(map.width());
    const int height = static_cast<int>(map.height());
    std::vector<bool> reachable(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                                false);
    const auto flat_index = [width](Coordinates position) {
        return static_cast<std::size_t>(position.y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(position.x);
    };

    std::vector<Coordinates> frontier;
    const Coordinates spawn = map.spawn();
    reachable[flat_index(spawn)] = true;
    frontier.push_back(spawn);
    for (std::size_t head = 0; head < frontier.size(); ++head) {
        const Coordinates current = frontier[head];
        for (const Coordinates offset : kCardinalOffsets) {
            const Coordinates neighbour = current + offset;
            if (!map.contains(neighbour)) {
                continue;
            }
            if (!is_walkable(map.terrain_at(neighbour))) {
                continue;
            }
            const std::size_t index = flat_index(neighbour);
            if (reachable[index]) {
                continue;
            }
            reachable[index] = true;
            frontier.push_back(neighbour);
        }
    }
    return reachable;
}

}  // namespace

std::uint64_t count_reachable_walkable_cells(const Map& map) {
    const std::vector<bool> reachable = reachable_mask(map);
    std::uint64_t count = 0;
    for (const bool cell : reachable) {
        if (cell) {
            ++count;
        }
    }
    return count;
}

std::uint64_t count_explored_reachable_walkable_cells(const Map& map,
                                                      const VisibilityMap& visibility) {
    const int width = static_cast<int>(map.width());
    const int height = static_cast<int>(map.height());
    const std::vector<bool> reachable = reachable_mask(map);
    std::uint64_t count = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);
            if (!reachable[index]) {
                continue;
            }
            const Coordinates cell{x, y};
            if (visibility.contains(cell) && visibility.at(cell) != CellVisibility::unexplored) {
                ++count;
            }
        }
    }
    return count;
}
