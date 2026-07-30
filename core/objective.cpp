#include "objective.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "terrain.h"
#include "world_generation.h"

namespace {

// The 16 deterministic first-word entries, in the exact required order. The
// low nibble of the name hash selects one.
constexpr std::array<const char*, 16> kFirstWords{
    "Ashen", "Bright", "Cloud", "Dawn", "Ember", "Glass", "Iron", "Moon",
    "North", "Rain", "Silent", "Star", "Storm", "Sun", "White", "Wild"};

// The 16 deterministic second-word entries, in the exact required order. Bits
// 8..11 of the name hash select one.
constexpr std::array<const char*, 16> kSecondWords{
    "Crown", "Ford", "Gate", "Harbor", "Hollow", "Lantern", "Light", "Pass",
    "Peak", "Reach", "River", "Spire", "Stone", "Vale", "Watch", "Way"};

// The four cardinal neighbour offsets. Order does not affect the selected cell:
// distances are uniform and the exit is chosen from a separately collected
// row-major candidate pool, so breadth-first neighbour order is never the
// tie-breaker.
constexpr std::array<Coordinates, 4> kCardinalOffsets{
    Coordinates{0, -1}, Coordinates{0, 1}, Coordinates{-1, 0}, Coordinates{1, 0}};

// Compute, for every cell, the minimum number of successful cardinal moves from
// spawn over walkable terrain, or -1 when the cell is unreachable. Movement cost
// magnitude never affects the distance: each successful step adds exactly one.
[[nodiscard]] std::vector<int> compute_distances(const Map& map) {
    const int width = static_cast<int>(map.width());
    const int height = static_cast<int>(map.height());
    std::vector<int> distance(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                              -1);

    const auto flat_index = [width](Coordinates position) {
        return static_cast<std::size_t>(position.y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(position.x);
    };

    // Iterative breadth-first search with an explicit queue (no recursion). A
    // read cursor over a growing vector keeps the traversal frontier in FIFO
    // order so every cell is reached by a shortest path.
    std::vector<Coordinates> frontier;
    const Coordinates spawn = map.spawn();
    distance[flat_index(spawn)] = 0;
    frontier.push_back(spawn);

    for (std::size_t head = 0; head < frontier.size(); ++head) {
        const Coordinates current = frontier[head];
        const int next_distance = distance[flat_index(current)] + 1;
        for (const Coordinates offset : kCardinalOffsets) {
            const Coordinates neighbour = current + offset;
            if (!map.contains(neighbour)) {
                continue;
            }
            if (!is_walkable(map.terrain_at(neighbour))) {
                continue;
            }
            const std::size_t neighbour_index = flat_index(neighbour);
            if (distance[neighbour_index] != -1) {
                continue;
            }
            distance[neighbour_index] = next_distance;
            frontier.push_back(neighbour);
        }
    }

    return distance;
}

[[nodiscard]] std::vector<Coordinates> shortest_path(const Map& map, Coordinates source,
                                                     Coordinates target) {
    const int width = static_cast<int>(map.width());
    const int height = static_cast<int>(map.height());
    const std::size_t cell_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const auto flat_index = [width](Coordinates position) {
        return static_cast<std::size_t>(position.y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(position.x);
    };
    const auto coordinate_at = [width](std::size_t index) {
        return Coordinates{static_cast<int>(index % static_cast<std::size_t>(width)),
                           static_cast<int>(index / static_cast<std::size_t>(width))};
    };

    const std::size_t source_index = flat_index(source);
    const std::size_t target_index = flat_index(target);
    std::vector<bool> visited(cell_count, false);
    std::vector<std::size_t> parent(cell_count, cell_count);
    std::vector<std::size_t> frontier;
    frontier.reserve(cell_count);
    visited[source_index] = true;
    frontier.push_back(source_index);

    for (std::size_t head = 0; head < frontier.size() && !visited[target_index]; ++head) {
        const std::size_t current_index = frontier[head];
        const Coordinates current = coordinate_at(current_index);
        for (const Coordinates offset : kCardinalOffsets) {
            const Coordinates neighbour = current + offset;
            if (!map.contains(neighbour) || !is_walkable(map.terrain_at(neighbour))) {
                continue;
            }
            const std::size_t neighbour_index = flat_index(neighbour);
            if (visited[neighbour_index]) {
                continue;
            }
            visited[neighbour_index] = true;
            parent[neighbour_index] = current_index;
            frontier.push_back(neighbour_index);
        }
    }

    std::vector<Coordinates> path;
    if (!visited[target_index]) {
        return path;
    }
    for (std::size_t current = target_index;; current = parent[current]) {
        path.push_back(coordinate_at(current));
        if (current == source_index) {
            break;
        }
    }
    std::reverse(path.begin(), path.end());
    return path;
}

// The deterministic cheapest stamina cost of a walkable cardinal path from
// `source` to `target`, where entering a cell costs stamina_cost_of(that cell).
// This is a Dijkstra shortest-path search: only the scalar minimum total cost is
// returned, so equal-cost path tie ordering can never affect the result. The
// source cell is free (it is already occupied); every step adds the destination
// terrain's entry cost. Returns 0 when source == target and the unreachable
// sentinel (uint64 max) when no walkable path exists.
[[nodiscard]] std::uint64_t minimum_travel_cost(const Map& map, Coordinates source,
                                                Coordinates target) {
    constexpr std::uint64_t unreachable = std::numeric_limits<std::uint64_t>::max();
    const int width = static_cast<int>(map.width());
    const int height = static_cast<int>(map.height());

    const auto flat_index = [width](Coordinates position) {
        return static_cast<std::size_t>(position.y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(position.x);
    };

    std::vector<std::uint64_t> best(static_cast<std::size_t>(width) *
                                        static_cast<std::size_t>(height),
                                    unreachable);
    std::vector<bool> settled(best.size(), false);
    best[flat_index(source)] = 0;

    // A plain array scan for the next unsettled minimum keeps the search free of
    // <queue>/<functional> and any comparator ordering: the settled cost of each
    // cell is a pure function of the terrain, independent of scan order.
    for (std::size_t processed = 0; processed < best.size(); ++processed) {
        std::size_t current = best.size();
        std::uint64_t current_cost = unreachable;
        for (std::size_t i = 0; i < best.size(); ++i) {
            if (!settled[i] && best[i] < current_cost) {
                current_cost = best[i];
                current = i;
            }
        }
        if (current == best.size()) {
            break;  // Every remaining cell is unreachable.
        }
        settled[current] = true;

        const Coordinates here{static_cast<int>(current % static_cast<std::size_t>(width)),
                               static_cast<int>(current / static_cast<std::size_t>(width))};
        for (const Coordinates offset : kCardinalOffsets) {
            const Coordinates neighbour = here + offset;
            if (!map.contains(neighbour)) {
                continue;
            }
            const std::optional<std::uint32_t> step = stamina_cost_of(map.terrain_at(neighbour));
            if (!step.has_value()) {
                continue;  // Impassable terrain is not a graph edge.
            }
            const std::size_t neighbour_index = flat_index(neighbour);
            if (settled[neighbour_index]) {
                continue;
            }
            const std::uint64_t candidate = current_cost + static_cast<std::uint64_t>(*step);
            if (candidate < best[neighbour_index]) {
                best[neighbour_index] = candidate;
            }
        }
    }

    return best[flat_index(target)];
}

// The canonical fingerprint hashed to select the exit among distant scenic
// candidates: the exact map text followed by the spawn coordinate. It contains
// only canonical map terrain glyphs and decimal spawn coordinates, never a map
// path, seed text, the original CLI seed text, the clock, environment state, or
// any mutable global state, so selection is a pure function of the map and spawn.
[[nodiscard]] std::string placement_fingerprint(const Map& map, Coordinates spawn) {
    std::string input = map.to_string();
    input += "\nspawn ";
    input += std::to_string(spawn.x);
    input += " ";
    input += std::to_string(spawn.y);
    return input;
}

// The canonical fingerprint hashed into the exit name: the exact map text, the
// spawn coordinate, and the exit coordinate. It contains only canonical map
// terrain glyphs and decimal coordinates, never a map path, seed text, or any
// mutable global state, so the name is a pure function of the placed objective.
[[nodiscard]] std::string name_fingerprint(const Map& map, Coordinates spawn,
                                           Coordinates landmark) {
    std::string input = map.to_string();
    input += "\nspawn ";
    input += std::to_string(spawn.x);
    input += " ";
    input += std::to_string(spawn.y);
    input += "\nlandmark ";
    input += std::to_string(landmark.x);
    input += " ";
    input += std::to_string(landmark.y);
    return input;
}

[[nodiscard]] std::string generate_landmark_name(const Map& map, Coordinates spawn,
                                               Coordinates landmark) {
    const std::uint64_t hash = hash_seed_text(name_fingerprint(map, spawn, landmark));
    const std::size_t first = static_cast<std::size_t>(hash & 0x0FULL);
    const std::size_t second = static_cast<std::size_t>((hash >> 8) & 0x0FULL);
    std::string name = kFirstWords[first];
    name += ' ';
    name += kSecondWords[second];
    name += " Landmark";
    return name;
}

[[nodiscard]] Direction bearing_to(Coordinates source, Coordinates target) noexcept {
    const int dx = target.x - source.x;
    const int dy = target.y - source.y;
    const int abs_x = dx < 0 ? -dx : dx;
    const int abs_y = dy < 0 ? -dy : dy;
    if (abs_x >= abs_y) {
        return dx < 0 ? Direction::left : Direction::right;
    }
    return dy < 0 ? Direction::up : Direction::down;
}

}  // namespace

LevelObjective create_level_objective(const Map& map) {
    const std::vector<int> distance = compute_distances(map);
    const Coordinates spawn = map.spawn();
    const int width = static_cast<int>(map.width());
    const int height = static_cast<int>(map.height());

    const auto flat_index = [width](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(x);
    };

    // The greatest nonnegative shortest-path distance among reachable cells.
    // Unreachable cells hold -1 and never raise it.
    int maximum_distance = 0;
    for (const int cell_distance : distance) {
        if (cell_distance > maximum_distance) {
            maximum_distance = cell_distance;
        }
    }

    Coordinates exit_cell = spawn;
    const bool authored = map.layout().exit && distance[flat_index(map.layout().exit->x,
                                                                   map.layout().exit->y)] > 0;
    // An authored exit is a level-template decision and always wins over
    // derivation, provided it is actually reachable from spawn. Everything else
    // below - the landmark, name, bearing, route cost, and status - is derived the
    // same way for authored and derived exits, so a template only chooses *where*
    // the level ends, never how the objective behaves.
    if (authored) {
        exit_cell = *map.layout().exit;
    } else if (maximum_distance > 0) {
        // When no cell is farther than spawn, spawn is the only reachable walkable
        // cell: the exit stays at spawn and the expedition starts completed. This
        // also avoids any threshold or modulo arithmetic in the trivial case.
        // The minimum eligible distance is the exact integer ceiling of 75% of
        // maximum_distance: subtracting the truncated quarter rounds the retained
        // three quarters up, so no floating point is involved.
        const int minimum_distance = maximum_distance - maximum_distance / 4;

        // Collect distant candidates in row-major order so the deterministic hash
        // selection is platform-independent. A candidate is reachable, walkable,
        // not spawn, and at least the minimum eligible distance. Scenic candidates
        // (hills and mountains as one pool) are preferred; the full distant pool
        // is the fallback when no scenic candidate is distant enough.
        std::vector<Coordinates> scenic;
        std::vector<Coordinates> distant;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int cell_distance = distance[flat_index(x, y)];
                if (cell_distance < minimum_distance) {
                    continue;
                }
                const Coordinates cell{x, y};
                if (cell == spawn) {
                    continue;
                }
                const Terrain terrain = map.terrain_at(cell);
                if (!is_walkable(terrain)) {
                    continue;
                }
                distant.push_back(cell);
                if (terrain == Terrain::hill || terrain == Terrain::mountain) {
                    scenic.push_back(cell);
                }
            }
        }

        const std::vector<Coordinates>& candidates = scenic.empty() ? distant : scenic;
        // A nontrivial map always yields at least the maximum-distance cell, so
        // the pool is never empty; the guard documents that and rules out any
        // modulo-by-zero on the selection below.
        if (!candidates.empty()) {
            const std::uint64_t placement_hash =
                hash_seed_text(placement_fingerprint(map, spawn));
            exit_cell = candidates[static_cast<std::size_t>(placement_hash % candidates.size())];
        }
    }

    LevelObjective objective;
    objective.exit_cell = exit_cell;
    const std::vector<Coordinates> path = shortest_path(map, spawn, exit_cell);
    objective.landmark = path.size() <= 2u ? spawn : path[path.size() / 2u];
    objective.name = generate_landmark_name(map, spawn, objective.landmark);
    objective.exit_bearing = bearing_to(objective.landmark, exit_cell);
    if (exit_cell == spawn) {
        objective.status = ObjectiveStatus::completed;
    } else if (objective.landmark == spawn) {
        objective.status = ObjectiveStatus::seeking_exit;
    } else {
        objective.status = ObjectiveStatus::seeking_landmark;
    }
    // The cheapest route is computed only after the landmark and exit are fixed,
    // so it can never influence selection or naming (RISK-001). Both legs use
    // stamina_cost_of as the sole terrain-entry cost, and they are summed
    // independently because entry costs are asymmetric. A level whose exit is at
    // spawn has a zero-cost route.
    if (exit_cell == spawn) {
        objective.minimum_route_stamina_cost = 0;
    } else {
        constexpr std::uint64_t unreachable = std::numeric_limits<std::uint64_t>::max();
        const std::uint64_t to_landmark = minimum_travel_cost(map, spawn, objective.landmark);
        const std::uint64_t to_exit = minimum_travel_cost(map, objective.landmark, exit_cell);
        objective.minimum_route_stamina_cost =
            (to_landmark == unreachable || to_exit == unreachable) ? 0 : to_landmark + to_exit;
    }

    std::uint64_t reachable = 0;
    for (const int cell_distance : distance) {
        if (cell_distance >= 0) {
            ++reachable;
        }
    }
    objective.total_reachable_walkable_cells = reachable;
    return objective;
}

ObjectiveTransition advance_objective(LevelObjective& objective, Coordinates actor) {
    if (objective.status == ObjectiveStatus::seeking_landmark && actor == objective.landmark) {
        objective.status = ObjectiveStatus::seeking_exit;
        return ObjectiveTransition::landmark_discovered;
    }
    if (objective.status == ObjectiveStatus::seeking_exit && actor == objective.exit_cell) {
        objective.status = ObjectiveStatus::completed;
        return ObjectiveTransition::level_completed;
    }
    return ObjectiveTransition::none;
}
