#include "objective.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "landmark.h"
#include "terrain.h"
#include "world_generation.h"

namespace {

// The 16 deterministic first-word entries, in the exact required order. The
// low nibble of the name hash selects one. The middle word is no longer shared:
// it comes from the landmark kind's own pool in landmark.h, so the three words
// describe one place instead of colliding ("Silent Ford Crossing", not "Silent
// Peak Ford").
constexpr std::array<const char*, 16> kFirstWords{
    "Ashen", "Bright", "Cloud", "Dawn", "Ember", "Glass", "Iron", "Moon",
    "North", "Rain", "Silent", "Star", "Storm", "Sun", "White", "Wild"};

// How far from the shortest route's midpoint a landmark may be nudged to stand on
// terrain that suits its kind. Small on purpose: the landmark must stay a natural
// waypoint of the route, so this varies which identity a seed offers without
// letting the landmark wander off into a corner of the map. Three rather than two
// because at two the biggest tier never offered a mountain within reach of its
// midpoint, and so never produced an overlook over sixty seeds; the widest detour
// this can add is six moves against route budgets in the hundreds.
constexpr int kLandmarkSearchRadius = 3;

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

// The length in moves of a shortest walkable cardinal path from `source` to
// `target`. Every step costs exactly one, so this is a breadth-first search and
// only the scalar minimum is returned: path tie ordering can never affect the
// result. Returns 0 when source == target and the unreachable sentinel (uint64
// max) when no walkable path exists.
[[nodiscard]] std::uint64_t minimum_travel_length(const Map& map, Coordinates source,
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
    best[flat_index(source)] = 0;

    // Iterative breadth-first search with an explicit queue (no recursion). A read
    // cursor over a growing vector keeps the frontier in FIFO order, so the first
    // time a cell is reached is by a shortest path.
    std::vector<Coordinates> frontier;
    frontier.push_back(source);
    for (std::size_t head = 0; head < frontier.size(); ++head) {
        const Coordinates here = frontier[head];
        const std::uint64_t next_length = best[flat_index(here)] + 1u;
        for (const Coordinates offset : kCardinalOffsets) {
            const Coordinates neighbour = here + offset;
            if (!map.contains(neighbour)) {
                continue;
            }
            if (!is_walkable(map.terrain_at(neighbour))) {
                continue;  // Impassable terrain is not a graph edge.
            }
            const std::size_t neighbour_index = flat_index(neighbour);
            if (best[neighbour_index] != unreachable) {
                continue;
            }
            best[neighbour_index] = next_length;
            frontier.push_back(neighbour);
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
                                                 Coordinates landmark, LandmarkKind kind) {
    const std::uint64_t hash = hash_seed_text(name_fingerprint(map, spawn, landmark));
    const std::size_t first = static_cast<std::size_t>(hash & 0x0FULL);
    // Three bits for an eight-entry pool. Taken from bits 8..10 so the two word
    // choices stay independent, as they were when both pools held sixteen.
    const std::size_t second = static_cast<std::size_t>((hash >> 8) & 0x07ULL);
    std::string name = kFirstWords[first];
    name += ' ';
    name += std::string(landmark_words(kind)[second]);
    name += ' ';
    name += std::string(landmark_noun(kind));
    return name;
}

// Choose where the landmark stands, given the shortest route's midpoint. Every
// walkable, reachable cell within kLandmarkSearchRadius of the midpoint is a
// candidate, excluding spawn and the exit, which own their own meaning. A seeded
// hash first picks a kind from those the candidate terrains actually offer, then
// picks a cell among the candidates carrying that kind's terrain.
//
// Both steps iterate fixed orders - `all_landmark_kinds` and row-major cells - so
// the choice is a pure function of the map and never depends on traversal or
// container order. The midpoint is always a candidate, so this can only ever
// return a cell, never fail.
[[nodiscard]] Coordinates choose_landmark(const Map& map, const std::vector<int>& distance,
                                          Coordinates spawn, Coordinates exit_cell,
                                          Coordinates midpoint) {
    const int width = static_cast<int>(map.width());
    const auto flat_index = [width](Coordinates position) {
        return static_cast<std::size_t>(position.y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(position.x);
    };

    std::vector<Coordinates> candidates;
    for (int y = midpoint.y - kLandmarkSearchRadius; y <= midpoint.y + kLandmarkSearchRadius; ++y) {
        for (int x = midpoint.x - kLandmarkSearchRadius; x <= midpoint.x + kLandmarkSearchRadius;
             ++x) {
            const Coordinates cell{x, y};
            if (!map.contains(cell) || cell == spawn || cell == exit_cell) {
                continue;
            }
            if (!is_walkable(map.terrain_at(cell)) || distance[flat_index(cell)] < 0) {
                continue;
            }
            candidates.push_back(cell);
        }
    }
    if (candidates.empty()) {
        return midpoint;
    }

    std::vector<LandmarkKind> offered;
    for (const LandmarkKind kind : all_landmark_kinds) {
        for (const Coordinates cell : candidates) {
            if (map.terrain_at(cell) == terrain_of(kind)) {
                offered.push_back(kind);
                break;
            }
        }
    }
    if (offered.empty()) {
        return midpoint;
    }

    const std::uint64_t hash = hash_seed_text(name_fingerprint(map, spawn, midpoint) + "\nkind");
    const LandmarkKind chosen = offered[static_cast<std::size_t>(hash % offered.size())];

    // Among the cells carrying the chosen kind, take the closest to the midpoint,
    // so the detour bought by an identity is always the smallest one available. A
    // level of uniform terrain offers exactly one kind and its midpoint already
    // carries it at distance zero, so the landmark never moves without buying
    // something: the nudge exists to change what the landmark *is*, not where it is.
    int best = std::numeric_limits<int>::max();
    std::vector<Coordinates> matching;
    for (const Coordinates cell : candidates) {
        if (map.terrain_at(cell) != terrain_of(chosen)) {
            continue;
        }
        const int detour = std::abs(cell.x - midpoint.x) + std::abs(cell.y - midpoint.y);
        if (detour < best) {
            best = detour;
            matching.clear();
        }
        if (detour == best) {
            matching.push_back(cell);
        }
    }
    // `chosen` came from the terrains present among the candidates, so this is
    // never empty; the guard rules out a modulo by zero.
    if (matching.empty()) {
        return midpoint;
    }
    return matching[static_cast<std::size_t>((hash >> 32) % matching.size())];
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

std::vector<Coordinates> shortest_path(const Map& map, Coordinates source,
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
    // The route midpoint is the natural waypoint; the landmark is then nudged onto
    // nearby terrain that gives it an identity. The route length below is summed
    // through whatever cell that lands on, so a nudge that lengthens the route is
    // paid for honestly rather than hidden.
    objective.landmark = path.size() <= 2u
                             ? spawn
                             : choose_landmark(map, distance, spawn, exit_cell,
                                               path[path.size() / 2u]);
    objective.landmark_kind = landmark_kind_of(map.terrain_at(objective.landmark));
    objective.name = generate_landmark_name(map, spawn, objective.landmark,
                                            objective.landmark_kind);
    objective.exit_bearing = bearing_to(objective.landmark, exit_cell);
    if (exit_cell == spawn) {
        objective.status = ObjectiveStatus::completed;
    } else if (objective.landmark == spawn) {
        objective.status = ObjectiveStatus::seeking_exit;
    } else {
        objective.status = ObjectiveStatus::seeking_landmark;
    }
    // The shortest route is computed only after the landmark and exit are fixed,
    // so it can never influence selection or naming (RISK-001). Both legs are
    // summed because the route must pass through the landmark. A level whose exit
    // is at spawn has a zero-length route.
    if (exit_cell == spawn) {
        objective.minimum_route_length = 0;
    } else {
        constexpr std::uint64_t unreachable = std::numeric_limits<std::uint64_t>::max();
        const std::uint64_t to_landmark = minimum_travel_length(map, spawn, objective.landmark);
        const std::uint64_t to_exit = minimum_travel_length(map, objective.landmark, exit_cell);
        objective.minimum_route_length =
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
