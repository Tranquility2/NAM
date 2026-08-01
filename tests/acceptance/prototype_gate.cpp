#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coordinates.h"
#include "direction.h"
#include "expedition.h"
#include "expedition_score.h"
#include "game_state.h"
#include "level_feature.h"
#include "level_tier.h"
#include "objective.h"
#include "pcg32.h"
#include "terrain.h"

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

// Play one whole expedition, visiting every discovery when `thorough`. Returns
// the finished expedition so its carried totals can be inspected.
[[nodiscard]] Expedition play(std::uint64_t seed, bool thorough) {
    Expedition expedition(seed);
    while (!expedition.completed()) {
        if (thorough) {
            for (const Coordinates cell : discoveries_of(expedition.state())) {
                walk_to(expedition.state(), cell);
            }
        }
        walk_to(expedition.state(), expedition.state().objective().landmark);
        walk_to(expedition.state(), expedition.state().objective().exit_cell);
        REQUIRE(expedition.state().objective_completed());
        static_cast<void>(expedition.complete_level(LevelPerformance{}));
    }
    return expedition;
}

}  // namespace

TEST_SUITE("prototype_gate") {

TEST_CASE("every seeded prototype run is solvable end to end") {
    for (const std::uint64_t seed : gate_seeds()) {
        const Expedition finished = play(seed, /*thorough=*/false);
        CHECK(finished.completed());
        CHECK(finished.completed_levels() == finished.total_levels());
        REQUIRE(finished.summaries().size() == 2u);
        CHECK(finished.summaries()[0].tier == LevelTier::small);
        CHECK(finished.summaries()[1].tier == LevelTier::medium);
    }
}

TEST_CASE("the same seed replays the same prototype run exactly") {
    for (const std::uint64_t seed : gate_seeds()) {
        const Expedition first = play(seed, /*thorough=*/true);
        const Expedition second = play(seed, /*thorough=*/true);
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
    // every seed, even though the detour costs stamina the score penalizes.
    for (const std::uint64_t seed : gate_seeds()) {
        const Expedition direct = play(seed, /*thorough=*/false);
        const Expedition thorough = play(seed, /*thorough=*/true);

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
        const Expedition finished = play(seed, /*thorough=*/true);
        std::uint64_t summed = 0;
        for (const LevelSummary& summary : finished.summaries()) {
            const ExpeditionScore& score = summary.score;
            CHECK(score.value == score.route_value + score.discovery_value);
            CHECK(score.route_value <= completed_score_maximum);
            CHECK(score.discovery_value == score.discoveries_found * completed_score_per_discovery *
                                               score.discovery_multiplier);
            CHECK(score.excess_moves ==
                  (score.actual_moves > score.optimal_route_length
                       ? score.actual_moves - score.optimal_route_length
                       : 0));
            summed += score.value;
        }
        CHECK(finished.total_score() == summed);
    }
}

TEST_CASE("finding every discovery on a level earns the bonus for the next one") {
    for (const std::uint64_t seed : gate_seeds()) {
        const Expedition thorough = play(seed, /*thorough=*/true);
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

}  // TEST_SUITE("prototype_gate")
