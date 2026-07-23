#include "objective.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
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
// distances are uniform and the farthest cell is chosen in a separate row-major
// pass, so breadth-first neighbour order is never the tie-breaker.
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

BeaconObjective create_beacon_objective(const Map& map) {
    const std::vector<int> distance = compute_distances(map);
    const Coordinates spawn = map.spawn();
    const int width = static_cast<int>(map.width());
    const int height = static_cast<int>(map.height());

    // Select the farthest reachable cell in a separate row-major pass so the
    // choice is independent of breadth-first neighbour order: start from spawn
    // (distance 0) and replace the candidate only for a strictly greater
    // distance, so the earliest row-major cell wins any tie.
    Coordinates beacon = spawn;
    int best_distance = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t flat =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);
            const int cell_distance = distance[flat];
            if (cell_distance > best_distance) {
                best_distance = cell_distance;
                beacon = Coordinates{x, y};
            }
        }
    }

    BeaconObjective objective;
    objective.beacon = beacon;
    objective.name = generate_beacon_name(map, spawn, beacon);
    // When no cell is farther than spawn, spawn is the only reachable walkable
    // cell: the beacon sits at spawn and the expedition is already completed.
    objective.status =
        (beacon == spawn) ? ObjectiveStatus::completed : ObjectiveStatus::seeking_beacon;
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
