#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coordinates.h"
#include "direction.h"
#include "expedition.h"
#include "game_state.h"
#include "objective.h"
#include "visibility.h"

#include "scripted_walk.h"
#include "terrain.h"

namespace {

// Walk the actor to `target` along the core's deterministic shortest path,
// issuing real moves so every step charges stamina and advances the objective
// exactly as play would. When `moves` is given, every step taken is counted into
// it, so a test can report the run's real move total to complete_level rather
// than the zero a default LevelPerformance carries. Returns false when the target
// is unreachable.
bool walk_to(GameState& state, Coordinates target, std::uint64_t* moves = nullptr) {
    while (state.actor_position() != target) {
        const std::vector<Coordinates> path =
            shortest_path(state.map(), state.actor_position(), target);
        if (path.size() < 2u) {
            return false;
        }
        for (std::size_t step = 1; step < path.size(); ++step) {
            const Coordinates delta{path[step].x - path[step - 1u].x,
                                    path[step].y - path[step - 1u].y};
            const std::optional<Direction> direction = direction_of(delta);
            REQUIRE(direction.has_value());
            static_cast<void>(state.move(*direction));
            if (moves != nullptr) {
                ++*moves;
            }
        }
    }
    return true;
}

// Play the current level straight to its exit through the landmark, counting
// every move. This is the efficient run par is meant to reward.
bool finish_level_directly(GameState& state, std::uint64_t& moves) {
    return walk_to(state, state.objective().landmark, &moves) &&
           walk_to(state, state.objective().exit_cell, &moves) && state.objective_completed();
}

// Play the current level standing on every viewpoint before finishing, counting
// every move. This is the run the surveyor bonus is meant to reward.
bool finish_level_surveying(GameState& state, std::uint64_t& moves) {
    for (const LevelFeature& feature : state.features()) {
        if (feature.kind == LevelFeatureKind::vantage_point &&
            !walk_to(state, feature.position, &moves)) {
            return false;
        }
    }
    return finish_level_directly(state, moves);
}

// Play the current level to its exit through the landmark, without detouring.
bool finish_level(GameState& state) {
    return walk_to(state, state.objective().landmark) &&
           walk_to(state, state.objective().exit_cell) && state.objective_completed();
}

// Play the current level collecting every discovery first.
bool finish_level_collecting_everything(GameState& state) {
    for (const LevelFeature& feature : state.features()) {
        if (feature.kind == LevelFeatureKind::discovery && !walk_to(state, feature.position)) {
            return false;
        }
    }
    return finish_level(state);
}

// Uncover the entire level, then collect every discovery and finish. This is the
// thorough run the score is meant to reward, and the only kind that earns the
// carried bonus.
bool sweep_level(GameState& state) {
    while (const std::optional<Coordinates> target = nam::test::nearest_unexplored(state)) {
        if (!walk_to(state, *target)) return false;
    }
    return finish_level_collecting_everything(state);
}

constexpr std::uint64_t kSeed = 0x0F4289EAF4A1813Cull;

}  // namespace

TEST_SUITE("expedition") {

TEST_CASE("an expedition starts on the small tier with no carried state") {
    const Expedition expedition(kSeed);

    CHECK(expedition.numeric_seed() == kSeed);
    CHECK(expedition.current_tier() == LevelTier::small);
    CHECK(expedition.final_tier() == expedition_final_tier);
    CHECK(expedition.completed_levels() == 0u);
    CHECK(expedition.total_levels() == 4u);
    CHECK_FALSE(expedition.completed());
    CHECK(expedition.total_score() == 0u);
    CHECK(expedition.total_discoveries_found() == 0u);
    CHECK(expedition.active_bonus() == ExpeditionBonus::none);
    CHECK(expedition.summaries().empty());
    CHECK(expedition.state().map().width() == dimensions_of(LevelTier::small).width);
}

TEST_CASE("completing a level advances the tier and generates the next level") {
    Expedition expedition(kSeed);
    REQUIRE(finish_level(expedition.state()));

    const LevelTransition transition = expedition.complete_level(LevelPerformance{40u});

    CHECK(transition == LevelTransition::advanced);
    CHECK(expedition.current_tier() == LevelTier::medium);
    CHECK(expedition.completed_levels() == 1u);
    CHECK_FALSE(expedition.completed());
    // The next level is live and unplayed, standing on its own spawn.
    CHECK(expedition.state().map().width() == dimensions_of(LevelTier::medium).width);
    CHECK(expedition.state().actor_position() == expedition.state().map().spawn());
    CHECK_FALSE(expedition.state().objective_completed());
}

TEST_CASE("an expedition plays every tier in order and ends after the last one") {
    const LevelTier chain[] = {LevelTier::small, LevelTier::medium, LevelTier::large,
                               LevelTier::x_large};
    Expedition expedition(kSeed);
    REQUIRE(expedition.total_levels() == 4u);

    for (std::size_t level = 0; level + 1u < 4u; ++level) {
        CHECK(expedition.current_tier() == chain[level]);
        REQUIRE(finish_level(expedition.state()));
        REQUIRE(expedition.complete_level(LevelPerformance{}) == LevelTransition::advanced);
    }
    CHECK(expedition.current_tier() == LevelTier::x_large);
    REQUIRE(finish_level(expedition.state()));

    CHECK(expedition.complete_level(LevelPerformance{}) == LevelTransition::expedition_completed);
    CHECK(expedition.completed());
    CHECK(expedition.completed_levels() == 4u);
    REQUIRE(expedition.summaries().size() == 4u);
    for (std::size_t level = 0; level < 4u; ++level) {
        CHECK(expedition.summaries()[level].tier == chain[level]);
    }

    // A finished expedition never scores again.
    CHECK(expedition.complete_level(LevelPerformance{}) == LevelTransition::none);
    CHECK(expedition.completed_levels() == 4u);
    CHECK(expedition.summaries().size() == 4u);
}

TEST_CASE("completing a level before its objective finishes is an explicit no-op") {
    Expedition expedition(kSeed);
    REQUIRE_FALSE(expedition.state().objective_completed());

    CHECK(expedition.complete_level(LevelPerformance{10u}) == LevelTransition::none);
    CHECK(expedition.completed_levels() == 0u);
    CHECK(expedition.total_score() == 0u);
    CHECK(expedition.summaries().empty());
}

TEST_CASE("score and discoveries accumulate across the whole expedition") {
    Expedition expedition(kSeed);
    REQUIRE(finish_level(expedition.state()));
    REQUIRE(expedition.complete_level(LevelPerformance{30u}) == LevelTransition::advanced);

    const std::uint64_t after_first = expedition.total_score();
    const std::uint32_t available_first = expedition.total_discoveries_available();
    REQUIRE(after_first == expedition.summaries()[0].score.value);
    REQUIRE(available_first > 0u);

    REQUIRE(finish_level(expedition.state()));
    REQUIRE(expedition.complete_level(LevelPerformance{30u}) == LevelTransition::advanced);

    CHECK(expedition.total_score() ==
          after_first + expedition.summaries()[1].score.value);
    CHECK(expedition.total_discoveries_available() > available_first);
    CHECK(expedition.total_discoveries_found() ==
          expedition.summaries()[0].discoveries_found +
              expedition.summaries()[1].discoveries_found);
}

TEST_CASE("sweeping a level earns the bonus that doubles the next level's discoveries") {
    Expedition expedition(kSeed);
    REQUIRE(sweep_level(expedition.state()));
    REQUIRE(expedition.state().discoveries_found() == expedition.state().discovery_total());
    REQUIRE(expedition.state().discovery_total() > 0u);

    REQUIRE(expedition.complete_level(LevelPerformance{}) == LevelTransition::advanced);
    CHECK(expedition.summaries()[0].applied_bonus == ExpeditionBonus::none);
    CHECK(expedition.summaries()[0].score.explored_percent >= keen_eye_explored_percent);
    CHECK(expedition.summaries()[0].earned_bonus == ExpeditionBonus::keen_eye);
    CHECK(expedition.active_bonus() == ExpeditionBonus::keen_eye);

    REQUIRE(sweep_level(expedition.state()));
    REQUIRE(expedition.complete_level(LevelPerformance{}) == LevelTransition::advanced);

    const LevelSummary& second = expedition.summaries()[1];
    CHECK(second.applied_bonus == ExpeditionBonus::keen_eye);
    CHECK(second.score.discovery_multiplier == 2u);
    CHECK(second.score.discovery_value ==
          second.discoveries_found * completed_score_per_discovery * 2u);
}

TEST_CASE("collecting every discovery without uncovering the level earns nothing") {
    // The bonus is named for looking around, so walking to the level's discovery
    // and on to the exit must not be enough to earn it. Which seeds leave enough
    // ground unseen depends on where generation puts the discovery, so rather than
    // pin one seed and re-pin it whenever placement changes, this scans a small
    // band, requires that collecting everything really does leave some levels well
    // short, and checks that none of those short runs passed the keen eye test.
    // The other bonuses are tested by their own measures and are not the subject
    // here, so this asks the keen eye test directly rather than reading the one
    // bonus the level happened to carry.
    std::size_t short_runs = 0;
    for (std::uint64_t offset = 0; offset < 16u; ++offset) {
        Expedition expedition(kSeed + offset);
        REQUIRE(finish_level_collecting_everything(expedition.state()));
        REQUIRE(expedition.state().discoveries_found() == expedition.state().discovery_total());
        REQUIRE(expedition.complete_level(LevelPerformance{}) == LevelTransition::advanced);

        const LevelSummary& summary = expedition.summaries()[0];
        if (summary.score.explored_percent < keen_eye_explored_percent) {
            ++short_runs;
            CHECK_FALSE(earned_bonus_test(ExpeditionBonus::keen_eye, summary));
        }
    }
    CHECK(short_runs > 0u);
}

TEST_CASE("missing a discovery loses the keen eye bonus") {
    Expedition expedition(kSeed);
    REQUIRE(finish_level(expedition.state()));
    REQUIRE(expedition.state().discoveries_found() < expedition.state().discovery_total());

    REQUIRE(expedition.complete_level(LevelPerformance{}) == LevelTransition::advanced);
    CHECK_FALSE(earned_bonus_test(ExpeditionBonus::keen_eye, expedition.summaries()[0]));
}

TEST_CASE("the same seed replays the same expedition exactly") {
    Expedition first(kSeed);
    Expedition second(kSeed);

    REQUIRE(finish_level(first.state()));
    REQUIRE(finish_level(second.state()));
    REQUIRE(first.complete_level(LevelPerformance{25u}) == LevelTransition::advanced);
    REQUIRE(second.complete_level(LevelPerformance{25u}) == LevelTransition::advanced);

    CHECK(first.total_score() == second.total_score());
    CHECK(first.active_bonus() == second.active_bonus());
    CHECK(first.state().map().to_string() == second.state().map().to_string());
}

TEST_CASE("distinct seeds drive distinct expeditions") {
    const Expedition first(kSeed);
    const Expedition second(kSeed + 1u);

    CHECK(first.state().map().to_string() != second.state().map().to_string());
}

TEST_CASE("an expedition can be configured to run the full four-tier chain") {
    Expedition expedition(kSeed, LevelTier::x_large);

    CHECK(expedition.final_tier() == LevelTier::x_large);
    CHECK(expedition.total_levels() == 4u);

    for (const LevelTier tier : {LevelTier::small, LevelTier::medium, LevelTier::large}) {
        REQUIRE(expedition.current_tier() == tier);
        REQUIRE(finish_level(expedition.state()));
        REQUIRE(expedition.complete_level(LevelPerformance{}) == LevelTransition::advanced);
    }
    REQUIRE(expedition.current_tier() == LevelTier::x_large);
    REQUIRE(finish_level(expedition.state()));
    CHECK(expedition.complete_level(LevelPerformance{}) == LevelTransition::expedition_completed);
}

TEST_CASE("the bonus multiplier and identifier tables are total") {
    CHECK(discovery_multiplier_of(ExpeditionBonus::none) == 1u);
    CHECK(discovery_multiplier_of(ExpeditionBonus::keen_eye) == 2u);
    CHECK(std::string(to_string(ExpeditionBonus::none)) == "none");
    CHECK(std::string(to_string(ExpeditionBonus::keen_eye)) == "keen_eye");
    CHECK(std::string(to_string(ExpeditionBonus::surveyor)) == "surveyor");
    CHECK(std::string(to_string(ExpeditionBonus::pathfinder)) == "pathfinder");
}

TEST_CASE("every bonus sharpens exactly the one thing it was earned for") {
    // The three bonuses have to stay one line each to read. That holds only if no
    // bonus reaches into more than one of the three grants, and no grant is
    // handed out by more than one bonus.
    for (const ExpeditionBonus bonus : earnable_bonuses) {
        const int grants = (discovery_multiplier_of(bonus) > 1u ? 1 : 0) +
                           (budget_multiplier_of(bonus) > 1u ? 1 : 0) +
                           (vantage_reveal_bonus_of(bonus) > 0 ? 1 : 0);
        CHECK(grants == 1);
    }
    CHECK(discovery_multiplier_of(ExpeditionBonus::keen_eye) == 2u);
    CHECK(budget_multiplier_of(ExpeditionBonus::pathfinder) == 2u);
    CHECK(vantage_reveal_bonus_of(ExpeditionBonus::surveyor) == surveyor_vantage_reveal_bonus);

    // No bonus at all must change nothing.
    CHECK(discovery_multiplier_of(ExpeditionBonus::none) == 1u);
    CHECK(budget_multiplier_of(ExpeditionBonus::none) == 1u);
    CHECK(vantage_reveal_bonus_of(ExpeditionBonus::none) == 0);
}

TEST_CASE("a level that earned several bonuses carries the hardest one") {
    // The precedence order is fixed and documented, so a perfect level still
    // hands the player a single line rather than an inventory.
    LevelSummary perfect;
    perfect.discovery_total = 2;
    perfect.discoveries_found = 2;
    perfect.vantage_total = 2;
    perfect.vantages_reached = 2;
    perfect.score.explored_percent = 100;
    perfect.score.par_moves = 40;
    perfect.score.actual_moves = 10;
    for (const ExpeditionBonus bonus : earnable_bonuses) {
        CHECK(earned_bonus_test(bonus, perfect));
    }
    CHECK(earned_bonus_of(perfect) == ExpeditionBonus::keen_eye);

    LevelSummary without_sweep = perfect;
    without_sweep.score.explored_percent = keen_eye_explored_percent - 1u;
    CHECK(earned_bonus_of(without_sweep) == ExpeditionBonus::surveyor);

    LevelSummary rushed = without_sweep;
    rushed.vantages_reached = 1;
    CHECK(earned_bonus_of(rushed) == ExpeditionBonus::pathfinder);

    LevelSummary nothing = rushed;
    nothing.score.actual_moves = nothing.score.par_moves + 1u;
    CHECK(earned_bonus_of(nothing) == ExpeditionBonus::none);
}

TEST_CASE("a bonus cannot be earned on a level that offered nothing to earn it on") {
    // Every test is written against a denominator, so a level with no discoveries
    // no viewpoints or no par to beat awards nothing by default.
    LevelSummary empty;
    empty.score.explored_percent = 100;
    for (const ExpeditionBonus bonus : earnable_bonuses) {
        CHECK_FALSE(earned_bonus_test(bonus, empty));
    }
    CHECK(earned_bonus_of(empty) == ExpeditionBonus::none);
    CHECK_FALSE(earned_bonus_test(ExpeditionBonus::none, empty));
}

TEST_CASE("standing on every viewpoint earns the surveyor bonus") {
    Expedition expedition(kSeed);
    REQUIRE(expedition.state().vantage_total() > 0u);

    std::uint64_t moves = 0;
    REQUIRE(finish_level_surveying(expedition.state(), moves));
    REQUIRE(expedition.state().vantages_reached() == expedition.state().vantage_total());

    REQUIRE(expedition.complete_level(LevelPerformance{moves}) == LevelTransition::advanced);
    const LevelSummary& first = expedition.summaries()[0];
    CHECK(first.vantage_total == first.vantages_reached);
    CHECK(earned_bonus_test(ExpeditionBonus::surveyor, first));
}

TEST_CASE("the surveyor bonus reaches into the next level's viewpoints") {
    // The grant is the only thing that crosses a level boundary besides the
    // score, and it must arrive as extra reach on the actual viewpoints rather
    // than as a number nothing reads.
    const GameState plain = make_level_state(LevelTier::medium, kSeed);
    const GameState surveyed =
        make_level_state(LevelTier::medium, kSeed, vantage_reveal_bonus_of(ExpeditionBonus::surveyor));

    CHECK(plain.vantage_reveal_bonus() == 0);
    CHECK(surveyed.vantage_reveal_bonus() == surveyor_vantage_reveal_bonus);
    // The same seed still builds the same level: the bonus changes what a
    // viewpoint shows, never where generation put anything.
    CHECK(plain.render() == surveyed.render());
    CHECK(plain.objective().name == surveyed.objective().name);
}

TEST_CASE("walking a level inside par earns the pathfinder bonus and doubles the next budget award") {
    Expedition expedition(kSeed);
    std::uint64_t moves = 0;
    REQUIRE(finish_level_directly(expedition.state(), moves));
    REQUIRE(expedition.complete_level(LevelPerformance{moves}) == LevelTransition::advanced);

    const LevelSummary& first = expedition.summaries()[0];
    REQUIRE(first.score.par_moves > 0u);
    REQUIRE(first.score.actual_moves <= first.score.par_moves);
    CHECK(earned_bonus_test(ExpeditionBonus::pathfinder, first));
    CHECK(first.score.budget_multiplier == 1u);

    std::uint64_t second_moves = 0;
    REQUIRE(finish_level_directly(expedition.state(), second_moves));
    REQUIRE(expedition.complete_level(LevelPerformance{second_moves}) ==
            LevelTransition::advanced);

    const LevelSummary& second = expedition.summaries()[1];
    CHECK(second.applied_bonus == expedition.summaries()[0].earned_bonus);
    if (second.applied_bonus == ExpeditionBonus::pathfinder) {
        CHECK(second.score.budget_multiplier == 2u);
        CHECK(second.score.budget_value == completed_budget_award * 2u);
    }
}

TEST_CASE("par leaves room for a real run and still costs a wandering one") {
    // Par has to be reachable to mean anything, and losable to mean anything
    // else. Checked against real generated levels rather than arithmetic: the
    // optimal route must make par with room to spare on every tier, and a run
    // that uncovers the whole level must sometimes lose it.
    std::size_t sweeps_over_par = 0;
    for (const LevelTier tier :
         {LevelTier::small, LevelTier::medium, LevelTier::large, LevelTier::x_large}) {
        for (std::uint64_t offset = 0; offset < 4u; ++offset) {
            const std::uint64_t seed = kSeed + offset;

            GameState direct = make_level_state(tier, seed);
            std::uint64_t direct_moves = 0;
            REQUIRE(finish_level_directly(direct, direct_moves));
            const std::uint64_t par =
                par_moves_for(direct.objective().total_reachable_walkable_cells);
            REQUIRE(par > 0u);
            // Twice the optimal route still makes par, so a player who searches
            // under fog rather than walking a known map can earn this.
            CHECK(direct_moves * 2u <= par);

            GameState swept = make_level_state(tier, seed);
            std::uint64_t sweep_moves = 0;
            while (const std::optional<Coordinates> target = nam::test::nearest_unexplored(swept)) {
                REQUIRE(walk_to(swept, *target, &sweep_moves));
            }
            REQUIRE(finish_level_directly(swept, sweep_moves));
            CHECK(sweep_moves > direct_moves);
            if (sweep_moves > par) {
                ++sweeps_over_par;
            }
        }
    }
    CHECK(sweeps_over_par > 0u);
}

TEST_CASE("make_level_state builds a playable level for every tier") {
    for (const LevelTier tier : {LevelTier::small, LevelTier::medium, LevelTier::large,
                                 LevelTier::x_large}) {
        const GameState state = make_level_state(tier, kSeed);
        CHECK(state.map().width() == dimensions_of(tier).width);
        CHECK(state.map().height() == dimensions_of(tier).height);
        CHECK(state.actor_position() == center_spawn_of(tier));
        CHECK(state.discovery_total() > 0u);
    }
}

}  // TEST_SUITE("expedition")
