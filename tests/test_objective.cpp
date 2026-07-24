#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "coordinates.h"
#include "direction.h"
#include "game_event.h"
#include "game_state.h"
#include "map.h"
#include "map_parser.h"
#include "objective.h"
#include "terrain.h"
#include "world_generation.h"

namespace {

Map make_map(std::string_view text) {
    MapLoadResult result = load_map(text);
    REQUIRE(std::holds_alternative<Map>(result));
    return std::get<Map>(std::move(result));
}

const MoveAttemptedEvent& payload_of(const GameEvent& event) {
    return std::get<MoveAttemptedEvent>(event.data);
}

// The exact fixed name tables, mirrored here so the deterministic-name test can
// independently reconstruct the expected name from the public hash and the
// documented indices without trusting the implementation's own output.
constexpr std::array<const char*, 16> kFirstWords{
    "Ashen", "Bright", "Cloud", "Dawn", "Ember", "Glass", "Iron", "Moon",
    "North", "Rain", "Silent", "Star", "Storm", "Sun", "White", "Wild"};
constexpr std::array<const char*, 16> kSecondWords{
    "Crown", "Ford", "Gate", "Harbor", "Hollow", "Lantern", "Light", "Pass",
    "Peak", "Reach", "River", "Spire", "Stone", "Vale", "Watch", "Way"};

// The map-and-spawn fingerprint hashed to select the beacon, mirrored from the
// core so a test can independently reconstruct the deterministic candidate index.
std::string placement_fingerprint(const Map& map, Coordinates spawn) {
    std::string input = map.to_string();
    input += "\nspawn ";
    input += std::to_string(spawn.x);
    input += " ";
    input += std::to_string(spawn.y);
    return input;
}

std::string expected_name(const Map& map, Coordinates spawn, Coordinates beacon) {
    std::string input = placement_fingerprint(map, spawn);
    input += "\nbeacon ";
    input += std::to_string(beacon.x);
    input += " ";
    input += std::to_string(beacon.y);
    const std::uint64_t hash = hash_seed_text(input);
    std::string name = kFirstWords[static_cast<std::size_t>(hash & 0x0FULL)];
    name += ' ';
    name += kSecondWords[static_cast<std::size_t>((hash >> 8) & 0x0FULL)];
    name += " Beacon";
    return name;
}

// An independent BFS over the map so a test can verify the distance of any cell
// and reconstruct the exact distant candidate pool the placement policy uses.
std::vector<int> bfs_distances(const Map& map) {
    const int width = static_cast<int>(map.width());
    const int height = static_cast<int>(map.height());
    std::vector<int> distance(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                              -1);
    const auto flat = [width](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(x);
    };
    std::vector<Coordinates> frontier;
    const Coordinates spawn = map.spawn();
    distance[flat(spawn.x, spawn.y)] = 0;
    frontier.push_back(spawn);
    const std::array<Coordinates, 4> offsets{Coordinates{0, -1}, Coordinates{0, 1},
                                             Coordinates{-1, 0}, Coordinates{1, 0}};
    for (std::size_t head = 0; head < frontier.size(); ++head) {
        const Coordinates current = frontier[head];
        const int next = distance[flat(current.x, current.y)] + 1;
        for (const Coordinates offset : offsets) {
            const Coordinates neighbour = current + offset;
            if (!map.contains(neighbour) || !is_walkable(map.terrain_at(neighbour))) {
                continue;
            }
            const std::size_t index = flat(neighbour.x, neighbour.y);
            if (distance[index] == -1) {
                distance[index] = next;
                frontier.push_back(neighbour);
            }
        }
    }
    return distance;
}

int distance_at(const Map& map, Coordinates cell) {
    const std::vector<int> distance = bfs_distances(map);
    return distance[static_cast<std::size_t>(cell.y) * map.width() +
                    static_cast<std::size_t>(cell.x)];
}

// The minimum eligible distance the core uses: the exact integer ceiling of 75%
// of the greatest reachable distance.
int minimum_eligible(int maximum_distance) { return maximum_distance - maximum_distance / 4; }

// Reconstruct, in row-major order, the distant candidate pool the core selects
// among for a map: scenic (hill/mountain) distant cells if any exist, otherwise
// every distant walkable cell. Mirrors REQ-005 through REQ-008.
std::vector<Coordinates> distant_candidates(const Map& map) {
    const std::vector<int> distance = bfs_distances(map);
    const Coordinates spawn = map.spawn();
    const int width = static_cast<int>(map.width());
    const int height = static_cast<int>(map.height());
    int maximum_distance = 0;
    for (const int cell_distance : distance) {
        if (cell_distance > maximum_distance) {
            maximum_distance = cell_distance;
        }
    }
    const int threshold = minimum_eligible(maximum_distance);
    std::vector<Coordinates> scenic;
    std::vector<Coordinates> distant;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int cell_distance =
                distance[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)];
            if (cell_distance < threshold) {
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
    return scenic.empty() ? distant : scenic;
}

Coordinates expected_beacon(const Map& map) {
    const std::vector<Coordinates> candidates = distant_candidates(map);
    const std::uint64_t hash = hash_seed_text(placement_fingerprint(map, map.spawn()));
    return candidates[static_cast<std::size_t>(hash % candidates.size())];
}

}  // namespace

TEST_SUITE("game") {

TEST_CASE("the minimum eligible distance is the exact integer ceiling of 75 percent") {
    // TEST-001 / REQ-004: maximum_distance - maximum_distance / 4 equals the exact
    // ceiling of three quarters of the maximum distance for every maximum.
    for (int maximum = 1; maximum <= 64; ++maximum) {
        const int ceiling_three_quarters = (3 * maximum + 3) / 4;  // ceil(3m/4).
        CHECK(minimum_eligible(maximum) == ceiling_three_quarters);
    }
    // The documented sample thresholds hold: divisible and non-divisible maxima.
    CHECK(minimum_eligible(4) == 3);
    CHECK(minimum_eligible(8) == 6);
    CHECK(minimum_eligible(10) == 8);
    CHECK(minimum_eligible(5) == 4);
    CHECK(minimum_eligible(7) == 6);
}

TEST_CASE("a divisible maximum distance selects the hashed distant candidate") {
    // TEST-001 / TEST-004: a 9-cell corridor has maximum distance 8 (divisible by
    // four), so the minimum eligible distance is 6 and the distant pool is x=6,7,8.
    const Map map = make_map("NAM-MAP 1\nwidth 9\nheight 1\nspawn 0 0\n---\n.........\n");
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.beacon == expected_beacon(map));
    CHECK(distance_at(map, objective.beacon) >= minimum_eligible(8));
    CHECK(distance_at(map, objective.beacon) >= 6);
    CHECK(objective.status == ObjectiveStatus::seeking_beacon);
}

TEST_CASE("a non-divisible maximum distance rounds the threshold up") {
    // TEST-001 / TEST-004: a 6-cell corridor has maximum distance 5, so the exact
    // 75% ceiling is 4 and only x=4,5 are eligible; x=3 (distance 3) never is.
    const Map map = make_map("NAM-MAP 1\nwidth 6\nheight 1\nspawn 0 0\n---\n......\n");
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.beacon == expected_beacon(map));
    CHECK(distance_at(map, objective.beacon) >= 4);
    CHECK(objective.beacon.x >= 4);  // distance 3 at x=3 is below the threshold.
}

TEST_CASE("candidate order is row-major and the hash modulo selects the exact cell") {
    // TEST-004: independently rebuild the row-major distant pool for a corridor and
    // confirm the core selects candidates[placement_hash % size].
    const Map map = make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n");
    const std::vector<Coordinates> candidates = distant_candidates(map);
    REQUIRE(candidates.size() == 2);
    CHECK(candidates[0] == Coordinates{3, 0});  // row-major first.
    CHECK(candidates[1] == Coordinates{4, 0});
    const std::uint64_t hash = hash_seed_text(placement_fingerprint(map, map.spawn()));
    const Coordinates selected = candidates[static_cast<std::size_t>(hash % candidates.size())];
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.beacon == selected);
}

TEST_CASE("scenic distant cells exclude equally distant base terrain") {
    // TEST-002 / REQ-007: a hill and open ground sit at the same maximum distance
    // two on either side of a central spawn. The scenic pool holds only the hill,
    // so the beacon is the hill even though the open cell is equally distant.
    const Map map = make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 2 0\n---\n^..x.\n");
    REQUIRE(distance_at(map, Coordinates{0, 0}) == 2);  // the hill.
    REQUIRE(distance_at(map, Coordinates{4, 0}) == 2);  // equally distant open.
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.beacon == Coordinates{0, 0});
    CHECK(map.terrain_at(objective.beacon) == Terrain::hill);
}

TEST_CASE("scenic distant cells are preferred over a more distant base cell") {
    // TEST-002 / REQ-007: the only scenic distant cell is a hill at distance four;
    // an open cell at distance five is more distant but base, so it is excluded.
    const Map map = make_map("NAM-MAP 1\nwidth 5\nheight 2\nspawn 0 0\n---\n....^\n....x\n");
    REQUIRE(distance_at(map, Coordinates{4, 0}) == 4);  // hill.
    REQUIRE(distance_at(map, Coordinates{4, 1}) == 5);  // more distant base.
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.beacon == Coordinates{4, 0});
    CHECK(map.terrain_at(objective.beacon) == Terrain::hill);
}

TEST_CASE("base terrain is used when no distant scenic candidate exists") {
    // TEST-003 / REQ-008: a corridor of open ground has no scenic cell, so the full
    // distant pool is the fallback and the selected beacon is walkable base terrain.
    const Map map = make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n");
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.beacon == expected_beacon(map));
    CHECK(objective.beacon == Coordinates{3, 0});
    CHECK(map.terrain_at(objective.beacon) == Terrain::open);
    CHECK(is_walkable(map.terrain_at(objective.beacon)));
}

TEST_CASE("the selected cell need not be the earliest row-major candidate") {
    // TEST-005: a wall band forces a long detour; the distant pool is (0,2),(1,2),
    // (2,2) in row-major order, but the hash selects (2,2), which is neither the
    // earliest candidate nor the single farthest cell.
    const Map map =
        make_map("NAM-MAP 1\nwidth 5\nheight 3\nspawn 0 0\n---\n.....\n====.\n.....\n");
    const std::vector<Coordinates> candidates = distant_candidates(map);
    REQUIRE(candidates.front() == Coordinates{0, 2});  // earliest row-major.
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.beacon == expected_beacon(map));
    CHECK(objective.beacon == Coordinates{2, 2});
    CHECK(objective.beacon != candidates.front());  // not the earliest candidate.
    // (0,2) is strictly farther than the selected cell, so the beacon is not the
    // single farthest row-major cell either.
    CHECK(distance_at(map, Coordinates{0, 2}) > distance_at(map, objective.beacon));
    CHECK(is_walkable(map.terrain_at(objective.beacon)));
    CHECK(objective.status == ObjectiveStatus::seeking_beacon);
}

TEST_CASE("a symmetric open field places the beacon off the corners") {
    // TEST-005: on a symmetric five-by-five open field the old row-major farthest
    // rule always chose corner (0,0). The hash now selects an off-corner distant
    // cell, so a symmetric map no longer favours a corner.
    const Map map = make_map(
        "NAM-MAP 1\nwidth 5\nheight 5\nspawn 2 2\n---\n.....\n.....\n.....\n.....\n.....\n");
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.beacon == expected_beacon(map));
    CHECK(objective.beacon == Coordinates{0, 1});
    const bool is_corner = (objective.beacon == Coordinates{0, 0}) ||
                           (objective.beacon == Coordinates{4, 0}) ||
                           (objective.beacon == Coordinates{0, 4}) ||
                           (objective.beacon == Coordinates{4, 4});
    CHECK_FALSE(is_corner);
}

TEST_CASE("unreachable walkable cells are ignored when placing the beacon") {
    // REQ-005: cells at x=3,4 are walkable but sealed off by the wall at x=2, so
    // the only reachable distant cell is (0,0), left of the spawn.
    const Map map = make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 1 0\n---\n..=..\n");
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.beacon == Coordinates{0, 0});
    CHECK(is_walkable(map.terrain_at(objective.beacon)));
    CHECK(objective.status == ObjectiveStatus::seeking_beacon);
}

TEST_CASE("a single reachable spawn produces a completed beacon at spawn") {
    // TEST-006 / REQ-003: the only reachable walkable cell is the spawn, so the
    // beacon is placed at spawn with no candidate pool or modulo, and the objective
    // starts completed.
    const Map map = make_map("NAM-MAP 1\nwidth 3\nheight 1\nspawn 0 0\n---\n.=.\n");
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.beacon == map.spawn());
    CHECK(objective.status == ObjectiveStatus::completed);
    // A name is still generated for the spawn-is-beacon case.
    CHECK_FALSE(objective.name.empty());
}

TEST_CASE("placement never mutates terrain or map serialization") {
    // TEST-007: creating the objective is read-only over the map.
    const Map map = make_map(
        "NAM-MAP 1\nwidth 5\nheight 3\nspawn 0 0\n---\n.....\n====.\n.....\n");
    const std::string before = map.to_string();
    const BeaconObjective objective = create_beacon_objective(map);
    (void)objective;
    CHECK(map.to_string() == before);
}

TEST_CASE("beacon names use the exact fixed tables and hash indices") {
    // The generated name matches an independent reconstruction from the public
    // hash and documented indices, and is stable across repeated builds.
    const Map map = make_map(
        "NAM-MAP 1\nwidth 5\nheight 3\nspawn 0 0\n---\n.....\n====.\n.....\n");
    const BeaconObjective first = create_beacon_objective(map);
    const BeaconObjective second = create_beacon_objective(map);

    const std::string expected = expected_name(map, map.spawn(), first.beacon);
    CHECK(first.name == expected);
    CHECK(second.name == expected);
    CHECK(first.beacon == second.beacon);
    // The name ends with the fixed suffix and contains exactly two spaces.
    CHECK(first.name.size() > std::string(" Beacon").size());
    CHECK(first.name.rfind(" Beacon") == first.name.size() - std::string(" Beacon").size());
}

TEST_CASE("distinct beacon coordinates change the deterministic name input") {
    // The fingerprint is the map/spawn/beacon triple, so a map produces an
    // independently reconstructable name for its selected beacon.
    const Map corridor = make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n");
    const BeaconObjective objective = create_beacon_objective(corridor);
    CHECK(objective.name == expected_name(corridor, corridor.spawn(), objective.beacon));
}

TEST_CASE("advance_objective marks discovery only when a move enters the beacon") {
    BeaconObjective objective;
    objective.beacon = Coordinates{4, 0};
    objective.status = ObjectiveStatus::seeking_beacon;
    const Coordinates spawn{0, 0};

    // Entering spawn before discovery changes nothing.
    CHECK(advance_objective(objective, spawn, spawn) == ObjectiveTransition::none);
    CHECK(objective.status == ObjectiveStatus::seeking_beacon);

    // A non-beacon cell changes nothing.
    CHECK(advance_objective(objective, Coordinates{3, 0}, spawn) == ObjectiveTransition::none);
    CHECK(objective.status == ObjectiveStatus::seeking_beacon);

    // Entering the beacon discovers it and flips to returning.
    CHECK(advance_objective(objective, Coordinates{4, 0}, spawn) ==
          ObjectiveTransition::beacon_discovered);
    CHECK(objective.status == ObjectiveStatus::returning_to_spawn);
}

TEST_CASE("advance_objective completes only on returning to spawn after discovery") {
    BeaconObjective objective;
    objective.beacon = Coordinates{4, 0};
    objective.status = ObjectiveStatus::returning_to_spawn;
    const Coordinates spawn{0, 0};

    // Leaving the beacon while returning changes nothing.
    CHECK(advance_objective(objective, Coordinates{3, 0}, spawn) == ObjectiveTransition::none);
    CHECK(objective.status == ObjectiveStatus::returning_to_spawn);

    // Returning to spawn completes the expedition.
    CHECK(advance_objective(objective, spawn, spawn) == ObjectiveTransition::expedition_completed);
    CHECK(objective.status == ObjectiveStatus::completed);

    // A completed objective never transitions again.
    CHECK(advance_objective(objective, Coordinates{4, 0}, spawn) == ObjectiveTransition::none);
    CHECK(objective.status == ObjectiveStatus::completed);
}

TEST_CASE("a GameState owns a seeking objective and exposes completion state") {
    GameState state(make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n"));
    CHECK(state.objective().status == ObjectiveStatus::seeking_beacon);
    CHECK(state.objective().beacon == Coordinates{3, 0});
    CHECK_FALSE(state.objective_completed());
}

TEST_CASE("a single-cell GameState starts already completed") {
    GameState state(make_map("NAM-MAP 1\nwidth 3\nheight 1\nspawn 0 0\n---\n.=.\n"));
    CHECK(state.objective().status == ObjectiveStatus::completed);
    CHECK(state.objective_completed());
    CHECK(state.objective().beacon == state.map().spawn());
}

TEST_CASE("moves carry exact before/after status and typed discovery/completion") {
    // Walk the corridor to the beacon at (3,0), then back to spawn, asserting the
    // objective update nested in each movement event.
    GameState state(make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n"));
    REQUIRE(state.objective().beacon == Coordinates{3, 0});

    // (0,0) -> (1,0): still seeking, no transition, equal before/after.
    const GameEvent e1 = state.move(Direction::right);
    CHECK(payload_of(e1).objective_update.before == ObjectiveStatus::seeking_beacon);
    CHECK(payload_of(e1).objective_update.after == ObjectiveStatus::seeking_beacon);
    CHECK(payload_of(e1).objective_update.transition == ObjectiveTransition::none);

    (void)state.move(Direction::right);  // (2,0)

    // (2,0) -> (3,0): enters the beacon, discovery, seeking -> returning.
    const GameEvent discover = state.move(Direction::right);
    CHECK(payload_of(discover).objective_update.before == ObjectiveStatus::seeking_beacon);
    CHECK(payload_of(discover).objective_update.after == ObjectiveStatus::returning_to_spawn);
    CHECK(payload_of(discover).objective_update.transition ==
          ObjectiveTransition::beacon_discovered);
    CHECK(state.objective().status == ObjectiveStatus::returning_to_spawn);

    (void)state.move(Direction::left);  // (2,0)
    (void)state.move(Direction::left);  // (1,0)

    // (1,0) -> (0,0): returns to spawn, completion, returning -> completed.
    const GameEvent complete = state.move(Direction::left);
    CHECK(payload_of(complete).objective_update.before == ObjectiveStatus::returning_to_spawn);
    CHECK(payload_of(complete).objective_update.after == ObjectiveStatus::completed);
    CHECK(payload_of(complete).objective_update.transition ==
          ObjectiveTransition::expedition_completed);
    CHECK(state.objective_completed());
}

TEST_CASE("returning to spawn before discovery does not complete or transition") {
    GameState state(make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n"));
    const GameEvent away = state.move(Direction::right);  // (1,0)
    CHECK(payload_of(away).objective_update.transition == ObjectiveTransition::none);
    const GameEvent back = state.move(Direction::left);  // (0,0) spawn, still seeking
    CHECK(payload_of(back).objective_update.transition == ObjectiveTransition::none);
    CHECK(payload_of(back).objective_update.before == ObjectiveStatus::seeking_beacon);
    CHECK(payload_of(back).objective_update.after == ObjectiveStatus::seeking_beacon);
    CHECK(state.objective().status == ObjectiveStatus::seeking_beacon);
}

TEST_CASE("blocked moves, rest, and peek never advance the objective") {
    GameState state(make_map("NAM-MAP 1\nwidth 2\nheight 2\nspawn 0 0\n---\n..\n..\n"));
    REQUIRE(state.objective().status == ObjectiveStatus::seeking_beacon);

    // A boundary-blocked move carries none with equal before/after and leaves
    // status unchanged.
    const GameEvent blocked = state.move(Direction::up);
    CHECK(payload_of(blocked).outcome.result == MoveResult::blocked_by_boundary);
    CHECK(payload_of(blocked).objective_update.transition == ObjectiveTransition::none);
    CHECK(payload_of(blocked).objective_update.before ==
          payload_of(blocked).objective_update.after);
    CHECK(state.objective().status == ObjectiveStatus::seeking_beacon);

    // Rest never moves the actor, so it cannot advance the objective.
    (void)state.rest();
    CHECK(state.objective().status == ObjectiveStatus::seeking_beacon);

    // Peek is pure and leaves the objective untouched.
    (void)state.peek(Direction::right);
    CHECK(state.objective().status == ObjectiveStatus::seeking_beacon);
}

}  // TEST_SUITE("game")
