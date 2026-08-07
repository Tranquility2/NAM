#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "coordinates.h"
#include "direction.h"
#include "expedition.h"
#include "expedition_score.h"
#include "exploration.h"
#include "game_event.h"
#include "game_state.h"
#include "level_feature.h"
#include "level_tier.h"
#include "map.h"
#include "vantage.h"
#include "objective.h"
#include "pcg32.h"
#include "terrain.h"
#include "visibility.h"

#include "scripted_walk.h"

// The Phase 1 prototype acceptance gate (roadmap section 9). Every criterion that
// can be decided by machine is asserted here over a spread of seeds, so the gate
// is re-run on every build rather than being a one-off manual review. The
// remaining criteria are playtest judgements about how a run feels and are not
// asserted: whether players pause to interpret the rules, and whether they want
// to start another run.
//
// These cases deliberately duplicate a little coverage from the focused suites.
// A gate is only useful if it fails on its own terms, without depending on which
// other test happened to notice the regression first.
//
// Two families of invariant sit underneath every criterion above and are asserted
// here in their own right, because the whole design now rests on them:
//
//   * Sight. Terrain governs how far the actor can see and governs nothing else.
//     What is revealed must be exactly what standing somewhere earns - never more,
//     and never less once earned.
//   * Connectivity. A level is a place to walk around, so every walkable cell and
//     every piece of placed content must be reachable from spawn, and the whole of
//     it must be uncoverable by walking. Sight and connectivity meet in that last
//     claim: it is what makes exploring a solvable goal rather than a hope.

namespace {

constexpr std::uint64_t kGateSeedCount = 60;

// A deterministic spread of run seeds. Drawn from the repository's own generator
// so the gate never depends on process randomness.
[[nodiscard]] std::vector<std::uint64_t> gate_seeds() {
    Pcg32 rng(0x5EEDFACE5EEDFACEull, 0x9E3779B97F4A7C15ull);
    std::vector<std::uint64_t> seeds;
    seeds.reserve(kGateSeedCount);
    for (std::uint64_t i = 0; i < kGateSeedCount; ++i) {
        seeds.push_back((static_cast<std::uint64_t>(rng.next_u32()) << 32) | rng.next_u32());
    }
    return seeds;
}

// Walk `state` from its current position to `target` along a shortest walkable
// path, returning the moves it took. Fails the case if no path exists, which is
// exactly the solvability criterion.
std::vector<Direction> walk_to(GameState& state, Coordinates target) {
    std::vector<Direction> taken;
    while (state.actor_position() != target) {
        const std::vector<Coordinates> path =
            shortest_path(state.map(), state.actor_position(), target);
        REQUIRE(path.size() >= 2u);
        for (std::size_t step = 1; step < path.size(); ++step) {
            const Coordinates delta{path[step].x - path[step - 1u].x,
                                    path[step].y - path[step - 1u].y};
            const std::optional<Direction> direction = direction_of(delta);
            REQUIRE(direction.has_value());
            static_cast<void>(state.move(*direction));
            taken.push_back(*direction);
        }
    }
    return taken;
}

// Every discovery cell a level placed, in placement order.
[[nodiscard]] std::vector<Coordinates> discoveries_of(const GameState& state) {
    std::vector<Coordinates> cells;
    for (const LevelFeature& feature : state.features()) {
        if (feature.kind == LevelFeatureKind::discovery) cells.push_back(feature.position);
    }
    return cells;
}

// Every tier in ascending order. Phase 2 generates all four, so the structural
// invariants below are asserted on the whole chain rather than on the two tiers
// the Phase 1 prototype happened to play.
constexpr std::array<LevelTier, 4> kAllTiers{LevelTier::small, LevelTier::medium,
                                             LevelTier::large, LevelTier::x_large};

// The per-move sight sweeps below re-scan the whole map on every step, so they run
// over a smaller slice of the seed set. The claims are structural: a violation is
// a rule error that shows on the first seed that meets it, not a rare event that
// needs sixty samples to catch.
constexpr std::size_t kSightSeedCount = 8;

// Every cell of the map, row-major.
[[nodiscard]] std::vector<Coordinates> all_cells(const Map& map) {
    std::vector<Coordinates> cells;
    cells.reserve(map.width() * map.height());
    for (std::size_t y = 0; y < map.height(); ++y) {
        for (std::size_t x = 0; x < map.width(); ++x) {
            cells.push_back(Coordinates{static_cast<int>(x), static_cast<int>(y)});
        }
    }
    return cells;
}

// True when `cell` lies inside the square of `radius` cells in each direction
// around `center` - exactly the region reveal_square covers.
[[nodiscard]] bool within_square(Coordinates center, Coordinates cell, int radius) noexcept {
    return std::abs(cell.x - center.x) <= radius && std::abs(cell.y - center.y) <= radius;
}

[[nodiscard]] std::uint64_t explored_count(const GameState& state) {
    return count_explored_reachable_walkable_cells(state.map(), state.visibility());
}

// Walk one step, holding the current heading until something refuses it and only
// then turning. Holding the heading matters: rotating on every step instead paces
// a two-by-two square forever, covering no ground and never meeting a wall, which
// is exactly the wander that would make the sweeps below assert nothing. Returns
// the event so the caller can read whether the step landed and whether it fired a
// wide reveal.
[[nodiscard]] GameEvent step_around(GameState& state, std::size_t& rotation) {
    constexpr std::array<Direction, 4> order{Direction::right, Direction::down, Direction::left,
                                             Direction::up};
    for (std::size_t i = 0; i < order.size(); ++i) {
        const Direction direction = order[(rotation + i) % order.size()];
        if (state.peek(direction).result == MoveResult::moved) {
            rotation = (rotation + i) % order.size();
            return state.move(direction);
        }
    }
    // Every generated level has somewhere to go; a dead end would be a bug in the
    // connectivity cases below rather than here.
    REQUIRE(false);
    return state.move(order[0]);
}

// How thoroughly a scripted run plays each level.
enum class Style {
    direct,    // Straight to the landmark and out.
    thorough,  // Every discovery, then out.
    sweep,     // Uncover the whole level, then every discovery, then out.
};

// Play one whole expedition in the given style, counting every move it takes so
// the soft budget is scored against a real figure rather than zero. Returns the
// finished expedition so its carried totals can be inspected.
[[nodiscard]] Expedition play(std::uint64_t seed, Style style) {
    Expedition expedition(seed);
    while (!expedition.completed()) {
        std::uint64_t moves = 0;
        if (style == Style::sweep) {
            while (const std::optional<Coordinates> target =
                       nam::test::nearest_unexplored(expedition.state())) {
                moves += walk_to(expedition.state(), *target).size();
            }
        }
        if (style != Style::direct) {
            for (const Coordinates cell : discoveries_of(expedition.state())) {
                moves += walk_to(expedition.state(), cell).size();
            }
        }
        moves += walk_to(expedition.state(), expedition.state().objective().landmark).size();
        moves += walk_to(expedition.state(), expedition.state().objective().exit_cell).size();
        REQUIRE(expedition.state().objective_completed());
        static_cast<void>(expedition.complete_level(LevelPerformance{moves}));
    }
    return expedition;
}

}  // namespace

TEST_SUITE("prototype_gate") {

TEST_CASE("every seeded prototype run is solvable end to end") {
    for (const std::uint64_t seed : gate_seeds()) {
        const Expedition finished = play(seed, Style::direct);
        CHECK(finished.completed());
        CHECK(finished.completed_levels() == finished.total_levels());
        REQUIRE(finished.summaries().size() == 2u);
        CHECK(finished.summaries()[0].tier == LevelTier::small);
        CHECK(finished.summaries()[1].tier == LevelTier::medium);
    }
}

TEST_CASE("the same seed replays the same prototype run exactly") {
    for (const std::uint64_t seed : gate_seeds()) {
        const Expedition first = play(seed, Style::thorough);
        const Expedition second = play(seed, Style::thorough);
        REQUIRE(first.summaries().size() == second.summaries().size());
        for (std::size_t i = 0; i < first.summaries().size(); ++i) {
            CHECK(first.summaries()[i].tier == second.summaries()[i].tier);
            CHECK(first.summaries()[i].score.value == second.summaries()[i].score.value);
            CHECK(first.summaries()[i].discoveries_found ==
                  second.summaries()[i].discoveries_found);
        }
        CHECK(first.total_score() == second.total_score());
        CHECK(first.total_discoveries_found() == second.total_discoveries_found());
    }
}

TEST_CASE("impassable terrain is the only thing that ever refuses a step") {
    // Movement is fluid: nothing but the map's edge and a barrier may block a
    // command. Wander towards the tightest sight available, which is the terrain a
    // player is most likely to be forced through, and check the equality holds on
    // every peek along the way.
    bool observed_terrain_block = false;
    for (const std::uint64_t seed : gate_seeds()) {
        GameState state = make_level_state(LevelTier::medium, seed);
        for (int step = 0; step < 200; ++step) {
            std::optional<Direction> tightest;
            int tightest_sight = 0;
            for (const Direction direction :
                 {Direction::up, Direction::down, Direction::left, Direction::right}) {
                const MoveOutcome outcome = state.peek(direction);
                if (outcome.result == MoveResult::blocked_by_boundary) continue;

                // The only legitimate refusal is impassable terrain. Authored
                // content and the route so far must never produce one.
                CHECK((outcome.result == MoveResult::moved) == is_walkable(outcome.terrain));
                if (outcome.result != MoveResult::moved) {
                    observed_terrain_block = true;
                    continue;
                }
                const int sight = visibility_radius_of(outcome.terrain);
                if (!tightest || sight < tightest_sight) {
                    tightest = direction;
                    tightest_sight = sight;
                }
            }
            REQUIRE(tightest.has_value());
            static_cast<void>(state.move(*tightest));
        }
    }
    // The equality above is only meaningful if the wander really did meet a barrier.
    CHECK(observed_terrain_block);
}

TEST_CASE("the first landmark points truthfully at the exit") {
    for (const std::uint64_t seed : gate_seeds()) {
        Expedition expedition(seed);
        while (!expedition.completed()) {
            const LevelObjective& objective = expedition.state().objective();
            const int dx = objective.exit_cell.x - objective.landmark.x;
            const int dy = objective.exit_cell.y - objective.landmark.y;
            bool truthful = false;
            switch (objective.exit_bearing) {
                case Direction::up:    truthful = dy < 0; break;
                case Direction::down:  truthful = dy > 0; break;
                case Direction::left:  truthful = dx < 0; break;
                case Direction::right: truthful = dx > 0; break;
            }
            CHECK(truthful);
            // The landmark also has to be somewhere worth walking to: never the
            // cell the actor already stands on.
            CHECK(objective.landmark != expedition.state().map().spawn());

            walk_to(expedition.state(), objective.landmark);
            walk_to(expedition.state(), expedition.state().objective().exit_cell);
            static_cast<void>(expedition.complete_level(LevelPerformance{}));
        }
    }
}

TEST_CASE("visiting every optional side route always pays for its detour") {
    // The criterion "players voluntarily enter optional side routes" only holds if
    // the arithmetic rewards it. A thorough run must out-score a direct one on
    // every seed, even though the detour costs moves the budget measures.
    for (const std::uint64_t seed : gate_seeds()) {
        const Expedition direct = play(seed, Style::direct);
        const Expedition thorough = play(seed, Style::thorough);

        REQUIRE(thorough.total_discoveries_available() > 0);
        CHECK(direct.total_discoveries_found() == 0);
        CHECK(thorough.total_discoveries_found() == thorough.total_discoveries_available());
        CHECK(thorough.total_score() > direct.total_score());
    }
}

TEST_CASE("a level score is exactly its published parts") {
    // "Players can explain why their score changed" requires the reported figures
    // to add up with no hidden term.
    for (const std::uint64_t seed : gate_seeds()) {
        const Expedition finished = play(seed, Style::thorough);
        std::uint64_t summed = 0;
        for (const LevelSummary& summary : finished.summaries()) {
            const ExpeditionScore& score = summary.score;
            CHECK(score.value == score.completion_value + score.exploration_value +
                                     score.discovery_value + score.budget_value);
            CHECK(score.completion_value == completed_exit_award);
            CHECK(score.exploration_value <= completed_exploration_maximum);
            CHECK(score.budget_value <= completed_budget_award * score.budget_multiplier);
            CHECK(score.discovery_value == score.discoveries_found * completed_score_per_discovery *
                                               score.discovery_multiplier);
            CHECK(score.move_budget == move_budget_for(score.total_reachable_cells));
            CHECK(score.par_moves == par_moves_for(score.total_reachable_cells));
            summed += score.value;
        }
        CHECK(finished.total_score() == summed);
    }
}

TEST_CASE("uncovering a level never scores below rushing through it") {
    // The design's central invariant, checked against real generated levels
    // rather than arithmetic alone: a run that uncovers the whole map takes many
    // more moves than the direct line, and must still score higher on every seed
    // and on every individual level.
    for (const std::uint64_t seed : gate_seeds()) {
        const Expedition rushed = play(seed, Style::direct);
        const Expedition swept = play(seed, Style::sweep);

        REQUIRE(rushed.summaries().size() == swept.summaries().size());
        for (std::size_t level = 0; level < swept.summaries().size(); ++level) {
            const ExpeditionScore& fast = rushed.summaries()[level].score;
            const ExpeditionScore& full = swept.summaries()[level].score;
            REQUIRE(full.actual_moves > fast.actual_moves);
            CHECK(full.explored_percent == 100);
            CHECK(full.value > fast.value);
        }
        CHECK(swept.total_score() > rushed.total_score());
    }
}

TEST_CASE("a full sweep stays comfortably inside the soft move budget") {
    // The budget is only generous if uncovering everything never touches it. If
    // this fails the budget is too tight, not the run too slow.
    for (const std::uint64_t seed : gate_seeds()) {
        const Expedition swept = play(seed, Style::sweep);
        for (const LevelSummary& summary : swept.summaries()) {
            CHECK(summary.score.moves_over_budget == 0);
            CHECK(summary.score.budget_value == completed_budget_award);
        }
    }
}

TEST_CASE("sweeping a level earns the bonus for the next one") {
    for (const std::uint64_t seed : gate_seeds()) {
        const Expedition thorough = play(seed, Style::sweep);
        REQUIRE(thorough.summaries().size() == 2u);
        CHECK(thorough.summaries()[0].earned_bonus == ExpeditionBonus::keen_eye);
        CHECK(thorough.summaries()[1].applied_bonus == ExpeditionBonus::keen_eye);
        // The bonus has to be visible in the arithmetic, not only in the label.
        CHECK(thorough.summaries()[1].score.discovery_multiplier == 2);
    }
}

TEST_CASE("the prototype run is a proportionate slice of the full four-tier chain") {
    // "A normal two-tier run scales toward a 10-15 minute four-tier expedition"
    // cannot be timed by machine, but the ground a run has to cover can be
    // measured. Explorable area must grow strictly with the tier on every seed,
    // and the prototype must be a real but minor part of the whole chain.
    std::vector<std::uint64_t> total_route_cost(4, 0);
    for (const std::uint64_t seed : gate_seeds()) {
        std::vector<std::uint64_t> reachable;
        for (const LevelTier tier :
             {LevelTier::small, LevelTier::medium, LevelTier::large, LevelTier::x_large}) {
            const GameState state = make_level_state(tier, seed);
            reachable.push_back(state.objective().total_reachable_walkable_cells);
            total_route_cost[index_of(tier)] += state.objective().minimum_route_length;
        }
        REQUIRE(reachable.size() == 4u);
        CHECK(reachable[0] < reachable[1]);
        CHECK(reachable[1] < reachable[2]);
        CHECK(reachable[2] < reachable[3]);

        const std::uint64_t prototype = reachable[0] + reachable[1];
        const std::uint64_t whole = prototype + reachable[2] + reachable[3];
        // Between a twentieth and two thirds of the full expedition: long enough to
        // judge the loop, short enough that the remaining tiers still matter.
        CHECK(prototype * 20u >= whole);
        CHECK(prototype * 3u <= whole * 2u);
    }

    // Route length is seeded content, so a single seed may invert two neighbouring
    // tiers. Across the whole seed set the trend must still climb.
    CHECK(total_route_cost[0] < total_route_cost[1]);
    CHECK(total_route_cost[1] < total_route_cost[2]);
    CHECK(total_route_cost[2] < total_route_cost[3]);
}

// ---------------------------------------------------------------------------
// Sight
// ---------------------------------------------------------------------------

TEST_CASE("terrain alone decides what the actor can currently see") {
    const std::vector<std::uint64_t> seeds = gate_seeds();
    for (const LevelTier tier : kAllTiers) {
        for (std::size_t i = 0; i < kSightSeedCount; ++i) {
            GameState state = make_level_state(tier, seeds[i]);
            const std::vector<Coordinates> cells = all_cells(state.map());
            std::size_t rotation = 0;
            for (int step = 0; step < 60; ++step) {
                const GameEvent event = step_around(state, rotation);
                const auto& attempt = std::get<MoveAttemptedEvent>(event.data);
                if (attempt.outcome.result != MoveResult::moved) continue;

                // A wide reveal is the one sanctioned exception, and it is always
                // announced on the event that granted it.
                const int radius =
                    attempt.granted_wide_reveal()
                        ? attempt.wide_reveal_radius
                        : visibility_radius_of(state.map().terrain_at(state.actor_position()));
                for (const Coordinates cell : cells) {
                    const bool visible = state.visibility().at(cell) == CellVisibility::visible;
                    CHECK(visible == within_square(state.actor_position(), cell, radius));
                }
            }
        }
    }
}

TEST_CASE("no ground is ever uncovered from somewhere the actor has not stood") {
    const std::vector<std::uint64_t> seeds = gate_seeds();
    for (const LevelTier tier : kAllTiers) {
        for (std::size_t i = 0; i < kSightSeedCount; ++i) {
            GameState state = make_level_state(tier, seeds[i]);
            const std::vector<Coordinates> cells = all_cells(state.map());

            // Every square the actor has earned so far, as (centre, radius) pairs.
            std::vector<std::pair<Coordinates, int>> earned;
            const auto record_stand = [&](int wide) {
                const Coordinates here = state.actor_position();
                earned.emplace_back(here, visibility_radius_of(state.map().terrain_at(here)));
                if (wide > 0) earned.emplace_back(here, wide);
            };
            record_stand(0);

            std::size_t rotation = 0;
            for (int step = 0; step < 60; ++step) {
                const GameEvent event = step_around(state, rotation);
                const auto& attempt = std::get<MoveAttemptedEvent>(event.data);
                if (attempt.outcome.result != MoveResult::moved) continue;
                record_stand(attempt.wide_reveal_radius);

                for (const Coordinates cell : cells) {
                    if (state.visibility().at(cell) == CellVisibility::unexplored) continue;
                    bool covered = false;
                    for (const auto& square : earned) {
                        if (within_square(square.first, cell, square.second)) {
                            covered = true;
                            break;
                        }
                    }
                    CHECK(covered);
                }
            }
        }
    }
}

TEST_CASE("explored ground is never forgotten") {
    const std::vector<std::uint64_t> seeds = gate_seeds();
    for (const LevelTier tier : kAllTiers) {
        for (std::size_t i = 0; i < kSightSeedCount; ++i) {
            GameState state = make_level_state(tier, seeds[i]);
            std::uint64_t explored = explored_count(state);
            CHECK(explored > 0u);
            std::size_t rotation = 0;
            for (int step = 0; step < 120; ++step) {
                static_cast<void>(step_around(state, rotation));
                const std::uint64_t now = explored_count(state);
                CHECK(now >= explored);
                explored = now;
            }
        }
    }
}

TEST_CASE("a blocked step changes nothing the actor can see") {
    const std::vector<std::uint64_t> seeds = gate_seeds();
    std::size_t blocked_attempts = 0;
    for (const LevelTier tier : kAllTiers) {
        for (std::size_t i = 0; i < kSightSeedCount; ++i) {
            GameState state = make_level_state(tier, seeds[i]);
            const std::vector<Coordinates> cells = all_cells(state.map());
            std::size_t rotation = 0;
            for (int step = 0; step < 60; ++step) {
                static_cast<void>(step_around(state, rotation));

                for (const Direction direction : {Direction::up, Direction::down, Direction::left,
                                                  Direction::right}) {
                    if (state.peek(direction).result == MoveResult::moved) continue;
                    std::vector<CellVisibility> before;
                    before.reserve(cells.size());
                    for (const Coordinates cell : cells) before.push_back(state.visibility().at(cell));
                    const Coordinates position_before = state.actor_position();

                    static_cast<void>(state.move(direction));
                    ++blocked_attempts;

                    CHECK(state.actor_position() == position_before);
                    for (std::size_t c = 0; c < cells.size(); ++c) {
                        CHECK(state.visibility().at(cells[c]) == before[c]);
                    }
                }
            }
        }
    }
    // The claim is only worth anything if blocked steps actually happen.
    CHECK(blocked_attempts > 0u);
}

TEST_CASE("a vantage point shows ground that standing there could not") {
    const std::vector<std::uint64_t> seeds = gate_seeds();
    std::size_t vantages_entered = 0;
    for (const LevelTier tier : kAllTiers) {
        for (std::size_t i = 0; i < kSightSeedCount; ++i) {
            GameState state = make_level_state(tier, seeds[i]);
            for (const LevelFeature& feature : state.features()) {
                if (feature.kind != LevelFeatureKind::vantage_point) continue;
                if (state.actor_position() == feature.position) continue;

                static_cast<void>(walk_to(state, feature.position));
                if (!(state.actor_position() == feature.position)) continue;
                ++vantages_entered;

                // Sight after the climb is the vantage's own square, which is
                // strictly wider than the radius of the ground it stands on - so
                // even a summit on a mountain shows more than the mountain does.
                const Terrain ground = state.map().terrain_at(feature.position);
                const int own = visibility_radius_of(ground);
                CHECK(own < vantage_reveal_radius_of(vantage_kind_of(ground)));
                std::uint64_t visible = 0;
                std::uint64_t beyond_own = 0;
                for (const Coordinates cell : all_cells(state.map())) {
                    if (state.visibility().at(cell) != CellVisibility::visible) continue;
                    ++visible;
                    if (!within_square(feature.position, cell, own)) ++beyond_own;
                }
                CHECK(visible > 0u);
                CHECK(beyond_own > 0u);
                break;
            }
        }
    }
    CHECK(vantages_entered > 0u);
}

// ---------------------------------------------------------------------------
// Connectivity
// ---------------------------------------------------------------------------

TEST_CASE("no walkable ground is ever cut off from spawn") {
    for (const LevelTier tier : kAllTiers) {
        for (const std::uint64_t seed : gate_seeds()) {
            const GameState state = make_level_state(tier, seed);
            const Map& map = state.map();

            std::uint64_t walkable = 0;
            for (const Coordinates cell : all_cells(map)) {
                if (is_walkable(map.terrain_at(cell))) ++walkable;
            }
            const std::uint64_t reachable = count_reachable_walkable_cells(map);

            CHECK(walkable > 0u);
            // Not merely "mostly connected": a generated level never leaves an
            // island of ground the player can see but never visit.
            CHECK(reachable == walkable);
            CHECK(state.objective().total_reachable_walkable_cells == reachable);
        }
    }
}

TEST_CASE("every placed thing sits on ground the player can walk to") {
    for (const LevelTier tier : kAllTiers) {
        for (const std::uint64_t seed : gate_seeds()) {
            const GameState state = make_level_state(tier, seed);
            const Map& map = state.map();

            // The previous case pins reachable == walkable, so walkability is the
            // reachability test here.
            std::vector<Coordinates> placed{state.map().spawn(), state.objective().landmark,
                                            state.objective().exit_cell};
            for (const LevelFeature& feature : state.features()) placed.push_back(feature.position);

            for (const Coordinates cell : placed) {
                REQUIRE(map.contains(cell));
                CHECK(is_walkable(map.terrain_at(cell)));
            }
        }
    }
}

TEST_CASE("the barriers that could sever a level are really there") {
    for (const LevelTier tier : kAllTiers) {
        std::size_t with_impassable = 0;
        const std::vector<std::uint64_t> seeds = gate_seeds();
        for (const std::uint64_t seed : seeds) {
            const GameState state = make_level_state(tier, seed);
            const Map& map = state.map();
            for (const Coordinates cell : all_cells(map)) {
                if (!is_walkable(map.terrain_at(cell))) {
                    ++with_impassable;
                    break;
                }
            }
        }
        // Full connectivity above would be a hollow promise on a level with nothing
        // to route around, so every generated level carries impassable terrain.
        CHECK(with_impassable == seeds.size());
    }
}

TEST_CASE("walking can uncover the whole of any level") {
    const std::vector<std::uint64_t> seeds = gate_seeds();
    for (const LevelTier tier : kAllTiers) {
        for (std::size_t i = 0; i < kSightSeedCount; ++i) {
            GameState state = make_level_state(tier, seeds[i]);
            const std::uint64_t reachable = state.objective().total_reachable_walkable_cells;

            // The sweep drains nearest_unexplored; if sight and connectivity agree,
            // it terminates with nothing left to find.
            std::optional<Coordinates> target = nam::test::nearest_unexplored(state);
            std::size_t guard = 0;
            const std::size_t limit = static_cast<std::size_t>(reachable) * 8u + 64u;
            while (target.has_value() && guard < limit) {
                static_cast<void>(walk_to(state, *target));
                target = nam::test::nearest_unexplored(state);
                ++guard;
            }
            CHECK(guard < limit);
            CHECK(!target.has_value());
            CHECK(explored_count(state) == reachable);
        }
    }
}

}  // TEST_SUITE("prototype_gate")
