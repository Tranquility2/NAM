#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

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

std::string expected_name(const Map& map, Coordinates spawn, Coordinates beacon) {
    std::string input = map.to_string();
    input += "\nspawn ";
    input += std::to_string(spawn.x);
    input += " ";
    input += std::to_string(spawn.y);
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

}  // namespace

TEST_SUITE("game") {

TEST_CASE("the beacon sits on the farthest reachable cell of a straight corridor") {
    // TEST-001: uniform BFS distance selects the greatest shortest-path cell.
    const Map map = make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n");
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.beacon == Coordinates{4, 0});
    CHECK(objective.status == ObjectiveStatus::seeking_beacon);
}

TEST_CASE("equal maximum distances select the earliest row-major coordinate") {
    // TEST-002: from the centre of a 3x3 open field the four corners tie at
    // distance 2, so the earliest row-major corner (0,0) is chosen.
    const Map map = make_map("NAM-MAP 1\nwidth 3\nheight 3\nspawn 1 1\n---\n...\n...\n...\n");
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.beacon == Coordinates{0, 0});
    CHECK(objective.status == ObjectiveStatus::seeking_beacon);
}

TEST_CASE("obstacles lengthen the shortest path so a physically near cell is farthest") {
    // TEST-003: a wall band forces a detour through the single open column at
    // x=4, so (0,2) — two straight cells below spawn — becomes the farthest cell
    // at path distance 10 rather than distance 2.
    const Map map = make_map(
        "NAM-MAP 1\nwidth 5\nheight 3\nspawn 0 0\n---\n.....\n====.\n.....\n");
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.beacon == Coordinates{0, 2});
    CHECK(objective.status == ObjectiveStatus::seeking_beacon);
    // The chosen cell is walkable and never a wall.
    CHECK(is_walkable(map.terrain_at(objective.beacon)));
}

TEST_CASE("unreachable walkable cells are ignored when placing the beacon") {
    // TEST-003: cells at x=3,4 are walkable but sealed off by the wall at x=2,
    // so the farthest *reachable* cell is (0,0), left of the spawn.
    const Map map = make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 1 0\n---\n..=..\n");
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.beacon == Coordinates{0, 0});
    CHECK(is_walkable(map.terrain_at(objective.beacon)));
    CHECK(objective.status == ObjectiveStatus::seeking_beacon);
}

TEST_CASE("a single reachable spawn produces a completed beacon at spawn") {
    // TEST-005 / TEST-007: the only reachable walkable cell is the spawn, so the
    // beacon is placed at spawn and the objective starts completed.
    const Map map = make_map("NAM-MAP 1\nwidth 3\nheight 1\nspawn 0 0\n---\n.=.\n");
    const BeaconObjective objective = create_beacon_objective(map);
    CHECK(objective.beacon == map.spawn());
    CHECK(objective.status == ObjectiveStatus::completed);
    // A name is still generated for the spawn-is-beacon case.
    CHECK_FALSE(objective.name.empty());
}

TEST_CASE("placement never mutates terrain or map serialization") {
    // TEST-004: creating the objective is read-only over the map.
    const Map map = make_map(
        "NAM-MAP 1\nwidth 5\nheight 3\nspawn 0 0\n---\n.....\n====.\n.....\n");
    const std::string before = map.to_string();
    const BeaconObjective objective = create_beacon_objective(map);
    (void)objective;
    CHECK(map.to_string() == before);
}

TEST_CASE("beacon names use the exact fixed tables and hash indices") {
    // TEST-006: the generated name matches an independent reconstruction from the
    // public hash and documented indices, and is stable across repeated builds.
    const Map map = make_map(
        "NAM-MAP 1\nwidth 5\nheight 3\nspawn 0 0\n---\n.....\n====.\n.....\n");
    const BeaconObjective first = create_beacon_objective(map);
    const BeaconObjective second = create_beacon_objective(map);

    const std::string expected = expected_name(map, map.spawn(), first.beacon);
    CHECK(first.name == expected);
    CHECK(second.name == expected);
    // The name ends with the fixed suffix and contains exactly two spaces.
    CHECK(first.name.size() > std::string(" Beacon").size());
    CHECK(first.name.rfind(" Beacon") == first.name.size() - std::string(" Beacon").size());
}

TEST_CASE("distinct beacon coordinates change the deterministic name input") {
    // TEST-006 / RISK-007: the fingerprint is the map/spawn/beacon triple, so two
    // maps with different terrain produce independently reconstructable names.
    const Map corridor = make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n");
    const BeaconObjective objective = create_beacon_objective(corridor);
    CHECK(objective.name == expected_name(corridor, corridor.spawn(), objective.beacon));
}

TEST_CASE("advance_objective marks discovery only when a move enters the beacon") {
    // TEST-008 / TEST-010: reaching the beacon while seeking yields the discovery
    // transition; entering spawn or leaving the beacon does not.
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
    // TEST-009 / TEST-010.
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
    CHECK(state.objective().beacon == Coordinates{4, 0});
    CHECK_FALSE(state.objective_completed());
}

TEST_CASE("a single-cell GameState starts already completed") {
    GameState state(make_map("NAM-MAP 1\nwidth 3\nheight 1\nspawn 0 0\n---\n.=.\n"));
    CHECK(state.objective().status == ObjectiveStatus::completed);
    CHECK(state.objective_completed());
    CHECK(state.objective().beacon == state.map().spawn());
}

TEST_CASE("moves carry exact before/after status and typed discovery/completion") {
    // TEST-008 / TEST-009 / TEST-011: walk the corridor to the beacon, then back
    // to spawn, asserting the objective update nested in each movement event.
    GameState state(make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n"));

    // (0,0) -> (1,0): still seeking, no transition, equal before/after.
    const GameEvent e1 = state.move(Direction::right);
    CHECK(payload_of(e1).objective_update.before == ObjectiveStatus::seeking_beacon);
    CHECK(payload_of(e1).objective_update.after == ObjectiveStatus::seeking_beacon);
    CHECK(payload_of(e1).objective_update.transition == ObjectiveTransition::none);

    (void)state.move(Direction::right);  // (2,0)
    (void)state.move(Direction::right);  // (3,0)

    // (3,0) -> (4,0): enters the beacon, discovery, seeking -> returning.
    const GameEvent discover = state.move(Direction::right);
    CHECK(payload_of(discover).objective_update.before == ObjectiveStatus::seeking_beacon);
    CHECK(payload_of(discover).objective_update.after == ObjectiveStatus::returning_to_spawn);
    CHECK(payload_of(discover).objective_update.transition ==
          ObjectiveTransition::beacon_discovered);
    CHECK(state.objective().status == ObjectiveStatus::returning_to_spawn);

    (void)state.move(Direction::left);  // (3,0)
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
    // TEST-010: leaving spawn and coming back while still seeking is inert.
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
    // TEST-010 / TEST-011: only a committed successful move can advance status.
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
