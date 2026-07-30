#include <doctest/doctest.h>

#include <cstdint>
#include <map>
#include <queue>
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

// The Phase 0 exit condition: one handcrafted level is completed from entry to
// exit using movement alone. `phase0-level.map` is a serpentine whose only
// vertical links alternate between the left and right edges, so the route must
// cross a hill band, a water band, a fields band, and a mountain band in order.
// The bands are deliberately expensive enough to empty the stamina meter, which
// proves stamina is pressure rather than a gate: no recovery command exists and
// no move is ever refused for want of stamina.

namespace {

std::string fixture(std::string_view name) {
    return std::string(NAM_FIXTURES_DIR) + "/" + std::string(name);
}

Map load_phase0_level() {
    MapLoadResult result = load_map_file(fixture("phase0-level.map"));
    const Map* map = std::get_if<Map>(&result);
    REQUIRE(map != nullptr);
    return *map;
}

// Ordering for the coordinate keys used by the breadth-first search below.
struct CoordinateLess {
    [[nodiscard]] bool operator()(Coordinates a, Coordinates b) const noexcept {
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    }
};

// The cardinal steps that walk from `from` to `to` over walkable terrain. The
// test derives the route itself so it exercises the same map the core does
// without hard-coding a keystroke script that map edits would silently break.
std::vector<Direction> path_between(const Map& map, Coordinates from, Coordinates to) {
    const Direction directions[] = {Direction::up, Direction::down, Direction::left,
                                    Direction::right};
    std::map<Coordinates, std::pair<Coordinates, Direction>, CoordinateLess> came_from;
    std::queue<Coordinates> frontier;
    frontier.push(from);
    came_from.emplace(from, std::make_pair(from, Direction::up));

    while (!frontier.empty()) {
        const Coordinates current = frontier.front();
        frontier.pop();
        if (current == to) break;
        for (const Direction direction : directions) {
            const Coordinates next = current + direction_delta(direction);
            if (!map.contains(next) || !is_walkable(map.terrain_at(next))) continue;
            if (came_from.find(next) != came_from.end()) continue;
            came_from.emplace(next, std::make_pair(current, direction));
            frontier.push(next);
        }
    }

    REQUIRE(came_from.find(to) != came_from.end());
    std::vector<Direction> steps;
    for (Coordinates cell = to; !(cell == from);) {
        const auto& link = came_from.at(cell);
        steps.push_back(link.second);
        cell = link.first;
    }
    return std::vector<Direction>(steps.rbegin(), steps.rend());
}

// One completed walk of the level: every ordered event plus the observations the
// Phase 0 exit condition depends on.
struct Walk {
    std::vector<GameEvent> events;
    std::uint32_t lowest_stamina = 0;
    std::uint64_t blocked_attempts = 0;
    std::uint64_t discoveries = 0;
    std::uint64_t completions = 0;
};

Walk walk_level(GameState& state) {
    Walk walk;
    walk.lowest_stamina = state.stamina();

    const std::vector<Direction> to_landmark =
        path_between(state.map(), state.actor_position(), state.objective().landmark);
    const std::vector<Direction> to_exit =
        path_between(state.map(), state.objective().landmark, state.objective().exit_cell);

    std::vector<Direction> route = to_landmark;
    route.insert(route.end(), to_exit.begin(), to_exit.end());

    for (const Direction direction : route) {
        const GameEvent event = state.move(direction);
        const auto* move = std::get_if<MoveAttemptedEvent>(&event.data);
        REQUIRE(move != nullptr);
        if (move->outcome.result != MoveResult::moved) ++walk.blocked_attempts;
        switch (move->objective_update.transition) {
            case ObjectiveTransition::landmark_discovered: ++walk.discoveries; break;
            case ObjectiveTransition::level_completed:     ++walk.completions; break;
            case ObjectiveTransition::none:                break;
        }
        if (state.stamina() < walk.lowest_stamina) walk.lowest_stamina = state.stamina();
        walk.events.push_back(event);
    }
    return walk;
}

}  // namespace

TEST_SUITE("game") {

TEST_CASE("the handcrafted phase 0 level places a landmark on the route to a distant exit") {
    const Map map = load_phase0_level();
    const GameState state(map);
    const LevelObjective& objective = state.objective();

    CHECK(map.spawn() == Coordinates{0, 1});
    CHECK(objective.status == ObjectiveStatus::seeking_landmark);
    CHECK(objective.landmark != map.spawn());
    CHECK(objective.exit_cell != map.spawn());
    CHECK(objective.exit_cell != objective.landmark);
    CHECK_FALSE(objective.name.empty());
    CHECK(objective.minimum_route_stamina_cost > 0);
    CHECK(objective.total_reachable_walkable_cells > 1);

    // The landmark sits on a shortest path, so routing through it costs exactly
    // the published route cost rather than a detour.
    const std::vector<Direction> to_landmark =
        path_between(map, map.spawn(), objective.landmark);
    const std::vector<Direction> to_exit =
        path_between(map, objective.landmark, objective.exit_cell);
    const std::vector<Direction> direct = path_between(map, map.spawn(), objective.exit_cell);
    CHECK(to_landmark.size() + to_exit.size() == direct.size());
}

TEST_CASE("the handcrafted phase 0 level is completed from entry to exit by movement alone") {
    const Map map = load_phase0_level();
    GameState state(map);
    const Walk walk = walk_level(state);

    CHECK(walk.blocked_attempts == 0);
    CHECK(walk.discoveries == 1);
    CHECK(walk.completions == 1);
    CHECK(state.objective().status == ObjectiveStatus::completed);
    CHECK(state.actor_position() == state.objective().exit_cell);
}

TEST_CASE("the phase 0 walk empties the stamina meter without ever blocking a move") {
    const Map map = load_phase0_level();
    GameState state(map);
    const Walk walk = walk_level(state);

    // The expensive bands drain the meter to nothing, and every step still lands.
    CHECK(walk.lowest_stamina == 0);
    CHECK(walk.blocked_attempts == 0);
    for (const GameEvent& event : walk.events) {
        const auto* move = std::get_if<MoveAttemptedEvent>(&event.data);
        REQUIRE(move != nullptr);
        CHECK(move->outcome.result == MoveResult::moved);
    }
}

TEST_CASE("the phase 0 walk emits exactly one ordered event per command") {
    const Map map = load_phase0_level();
    GameState state(map);
    const Walk walk = walk_level(state);

    REQUIRE_FALSE(walk.events.empty());
    for (std::size_t i = 0; i < walk.events.size(); ++i) {
        CHECK(walk.events[i].sequence == static_cast<std::uint64_t>(i));
    }
}

TEST_CASE("the phase 0 walk restores the stamina meter on first landmark entry") {
    const Map map = load_phase0_level();
    GameState state(map);
    const Walk walk = walk_level(state);

    bool checked = false;
    for (const GameEvent& event : walk.events) {
        const auto* move = std::get_if<MoveAttemptedEvent>(&event.data);
        REQUIRE(move != nullptr);
        if (move->objective_update.transition != ObjectiveTransition::landmark_discovered) continue;
        CHECK(move->outcome.stamina_after == state.max_stamina());
        checked = true;
    }
    CHECK(checked);
}

TEST_CASE("the phase 0 level replays byte-identically from the same fixture") {
    const Map map = load_phase0_level();
    GameState first(map);
    GameState second(map);
    const Walk a = walk_level(first);
    const Walk b = walk_level(second);

    REQUIRE(a.events.size() == b.events.size());
    CHECK(first.objective().name == second.objective().name);
    CHECK(first.objective().exit_cell == second.objective().exit_cell);
    CHECK(a.lowest_stamina == b.lowest_stamina);
    for (std::size_t i = 0; i < a.events.size(); ++i) {
        const auto* left = std::get_if<MoveAttemptedEvent>(&a.events[i].data);
        const auto* right = std::get_if<MoveAttemptedEvent>(&b.events[i].data);
        REQUIRE(left != nullptr);
        REQUIRE(right != nullptr);
        CHECK(a.events[i].sequence == b.events[i].sequence);
        CHECK(left->outcome.result == right->outcome.result);
        CHECK(left->outcome.to == right->outcome.to);
        CHECK(left->outcome.stamina_after == right->outcome.stamina_after);
    }
}

}  // TEST_SUITE("game")
