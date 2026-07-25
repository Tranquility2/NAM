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
// distances are uniform and the beacon is chosen from a separately collected
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

// The deterministic minimum number of provisions that make the round trip from
// spawn to the beacon and back feasible. It is a 0-1 shortest-path search (a
// deque-based BFS) over states (position, stamina 0..max, phase) where a move
// edge costs 0 provisions and needs stamina_cost_of(destination) stamina, and a
// rest edge costs 1 provision and recovers rest_recovery_of(current terrain)
// capped at `max_stamina` while stamina is below the cap. Phase advances from
// seeking to returning on the move that first lands on the beacon; the goal is a
// returning-phase state at spawn. Move order and the array-backed relaxation make
// the scalar result independent of tie ordering. Returns 0 when the beacon
// coincides with spawn.
[[nodiscard]] std::uint64_t minimum_required_provisions(const Map& map, Coordinates spawn,
                                                       Coordinates beacon,
                                                       std::uint32_t max_stamina) {
    if (beacon == spawn) {
        return 0;
    }
    constexpr std::uint64_t unreachable = std::numeric_limits<std::uint64_t>::max();
    const int width = static_cast<int>(map.width());
    const int levels = static_cast<int>(max_stamina) + 1;  // stamina values 0..max
    const auto state_index = [width, levels](Coordinates position, int stamina, int phase) {
        const std::size_t cell = static_cast<std::size_t>(position.y) *
                                     static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(position.x);
        return ((cell * static_cast<std::size_t>(levels) + static_cast<std::size_t>(stamina)) * 2) +
               static_cast<std::size_t>(phase);
    };

    const std::size_t state_count = static_cast<std::size_t>(width) *
                                    static_cast<std::size_t>(map.height()) *
                                    static_cast<std::size_t>(levels) * 2;
    std::vector<std::uint64_t> dist(state_count, unreachable);

    // Each queue entry pairs a state with the distance it was queued at, so a
    // stale duplicate (queued before a cheaper relaxation) is skipped on pop. The
    // 0-1 weights keep the deque ordered by nondecreasing distance, so the first
    // popped goal state carries the minimum provisions.
    std::deque<std::pair<std::uint64_t, std::size_t>> queue;
    const std::size_t start = state_index(spawn, static_cast<int>(max_stamina), 0);
    dist[start] = 0;
    queue.emplace_back(0, start);

    while (!queue.empty()) {
        const std::uint64_t d = queue.front().first;
        const std::size_t s = queue.front().second;
        queue.pop_front();
        if (d != dist[s]) {
            continue;  // Superseded by a cheaper relaxation.
        }

        // Decode the packed state.
        const int phase = static_cast<int>(s % 2);
        std::size_t rest = s / 2;
        const int stamina = static_cast<int>(rest % static_cast<std::size_t>(levels));
        rest /= static_cast<std::size_t>(levels);
        const Coordinates here{static_cast<int>(rest % static_cast<std::size_t>(width)),
                               static_cast<int>(rest / static_cast<std::size_t>(width))};

        // A returning-phase state at spawn is the completed round trip.
        if (phase == 1 && here == spawn) {
            return d;
        }

        // Rest edge (one provision) when stamina is below the cap and the terrain
        // can be rested on.
        if (stamina < static_cast<int>(max_stamina)) {
            const std::optional<std::uint32_t> recovery = rest_recovery_of(map.terrain_at(here));
            if (recovery.has_value()) {
                const int recovered =
                    std::min(static_cast<int>(max_stamina), stamina + static_cast<int>(*recovery));
                const std::size_t next = state_index(here, recovered, phase);
                if (d + 1 < dist[next]) {
                    dist[next] = d + 1;
                    queue.emplace_back(d + 1, next);
                }
            }
        }

        // Move edges (no provision) to affordable walkable cardinal neighbours.
        for (const Coordinates offset : kCardinalOffsets) {
            const Coordinates neighbour = here + offset;
            if (!map.contains(neighbour)) {
                continue;
            }
            const std::optional<std::uint32_t> step = stamina_cost_of(map.terrain_at(neighbour));
            if (!step.has_value() || stamina < static_cast<int>(*step)) {
                continue;  // Impassable or unaffordable with the current stamina.
            }
            const int next_stamina = stamina - static_cast<int>(*step);
            const int next_phase = (phase == 0 && neighbour == beacon) ? 1 : phase;
            const std::size_t next = state_index(neighbour, next_stamina, next_phase);
            if (d < dist[next]) {
                dist[next] = d;
                queue.emplace_front(d, next);
            }
        }
    }

    // A reachable beacon always yields a finite result; the fallback keeps a run
    // from ever being blocked if the goal is somehow unreachable.
    return 0;
}

// The canonical fingerprint hashed to select the beacon among distant scenic
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

// The canonical fingerprint hashed into the beacon name: the exact map text, the
// spawn coordinate, and the beacon coordinate. It contains only canonical map
// terrain glyphs and decimal coordinates, never a map path, seed text, or any
// mutable global state, so the name is a pure function of the placed objective.
[[nodiscard]] std::string name_fingerprint(const Map& map, Coordinates spawn,
                                           Coordinates beacon) {
    std::string input = map.to_string();
    input += "\nspawn ";
    input += std::to_string(spawn.x);
    input += " ";
    input += std::to_string(spawn.y);
    input += "\nbeacon ";
    input += std::to_string(beacon.x);
    input += " ";
    input += std::to_string(beacon.y);
    return input;
}

[[nodiscard]] std::string generate_beacon_name(const Map& map, Coordinates spawn,
                                               Coordinates beacon) {
    const std::uint64_t hash = hash_seed_text(name_fingerprint(map, spawn, beacon));
    const std::size_t first = static_cast<std::size_t>(hash & 0x0FULL);
    const std::size_t second = static_cast<std::size_t>((hash >> 8) & 0x0FULL);
    std::string name = kFirstWords[first];
    name += ' ';
    name += kSecondWords[second];
    name += " Beacon";
    return name;
}

}  // namespace

BeaconObjective create_beacon_objective(const Map& map, std::uint32_t max_stamina) {
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

    Coordinates beacon = spawn;
    // When no cell is farther than spawn, spawn is the only reachable walkable
    // cell: the beacon stays at spawn and the expedition starts completed. This
    // also avoids any threshold or modulo arithmetic in the trivial case.
    if (maximum_distance > 0) {
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
            beacon = candidates[static_cast<std::size_t>(placement_hash % candidates.size())];
        }
    }

    BeaconObjective objective;
    objective.beacon = beacon;
    objective.name = generate_beacon_name(map, spawn, beacon);
    objective.status =
        (beacon == spawn) ? ObjectiveStatus::completed : ObjectiveStatus::seeking_beacon;
    // The cheapest round trip is computed only after the beacon coordinate is
    // fixed, so it can never influence beacon selection or naming (RISK-001). Both
    // legs use stamina_cost_of as the sole terrain-entry cost, and the two
    // directions are summed independently because entry costs are asymmetric. A
    // beacon at spawn has a zero-cost round trip.
    if (beacon == spawn) {
        objective.minimum_round_trip_stamina_cost = 0;
    } else {
        const std::uint64_t outbound = minimum_travel_cost(map, spawn, beacon);
        const std::uint64_t inbound = minimum_travel_cost(map, beacon, spawn);
        constexpr std::uint64_t unreachable = std::numeric_limits<std::uint64_t>::max();
        objective.minimum_round_trip_stamina_cost =
            (outbound == unreachable || inbound == unreachable) ? 0 : outbound + inbound;
    }

    // The minimum provisions and the reachable-cell denominator are pure objective
    // properties computed only after the beacon is fixed, so neither can influence
    // selection or naming (RISK-100). total_reachable_walkable_cells counts every
    // BFS-reachable walkable cell including spawn.
    objective.minimum_required_provisions =
        minimum_required_provisions(map, spawn, beacon, max_stamina);
    std::uint64_t reachable = 0;
    for (const int cell_distance : distance) {
        if (cell_distance >= 0) {
            ++reachable;
        }
    }
    objective.total_reachable_walkable_cells = reachable;
    return objective;
}

ObjectiveTransition advance_objective(BeaconObjective& objective, Coordinates actor,
                                      Coordinates spawn) {
    if (objective.status == ObjectiveStatus::seeking_beacon && actor == objective.beacon) {
        objective.status = ObjectiveStatus::returning_to_spawn;
        return ObjectiveTransition::beacon_discovered;
    }
    if (objective.status == ObjectiveStatus::returning_to_spawn && actor == spawn) {
        objective.status = ObjectiveStatus::completed;
        return ObjectiveTransition::expedition_completed;
    }
    return ObjectiveTransition::none;
}
