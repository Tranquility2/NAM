#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coordinates.h"
#include "direction.h"
#include "expedition.h"
#include "game_state.h"
#include "objective.h"
#include "terrain.h"

namespace {

// Walk the actor to `target` along the core's deterministic shortest path,
// issuing real moves so every step charges stamina and advances the objective
// exactly as play would. Returns false when the target is unreachable.
bool walk_to(GameState& state, Coordinates target) {
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
        }
    }
    return true;
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

constexpr std::uint64_t kSeed = 0x0F4289EAF4A1813Cull;

}  // namespace

TEST_CASE("an expedition starts on the small tier with no carried state") {
    const Expedition expedition(kSeed);

    CHECK(expedition.numeric_seed() == kSeed);
    CHECK(expedition.current_tier() == LevelTier::small);
    CHECK(expedition.final_tier() == prototype_final_tier);
    CHECK(expedition.completed_levels() == 0u);
    CHECK(expedition.total_levels() == 2u);
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

    const LevelTransition transition = expedition.complete_level(LevelPerformance{40u, 3u});

    CHECK(transition == LevelTransition::advanced);
    CHECK(expedition.current_tier() == LevelTier::medium);
    CHECK(expedition.completed_levels() == 1u);
    CHECK_FALSE(expedition.completed());
    // The next level is live and unplayed, so stamina reset with it.
    CHECK(expedition.state().map().width() == dimensions_of(LevelTier::medium).width);
    CHECK(expedition.state().actor_position() == expedition.state().map().spawn());
    CHECK(expedition.state().stamina() == GameState::maximum_stamina);
    CHECK_FALSE(expedition.state().objective_completed());
}

TEST_CASE("the prototype expedition ends after the medium tier") {
    Expedition expedition(kSeed);
    REQUIRE(finish_level(expedition.state()));
    REQUIRE(expedition.complete_level(LevelPerformance{}) == LevelTransition::advanced);
    REQUIRE(finish_level(expedition.state()));

    CHECK(expedition.complete_level(LevelPerformance{}) == LevelTransition::expedition_completed);
    CHECK(expedition.completed());
    CHECK(expedition.completed_levels() == 2u);
    CHECK(expedition.summaries().size() == 2u);
    CHECK(expedition.summaries()[0].tier == LevelTier::small);
    CHECK(expedition.summaries()[1].tier == LevelTier::medium);

    // A finished expedition never scores again.
    CHECK(expedition.complete_level(LevelPerformance{}) == LevelTransition::none);
    CHECK(expedition.completed_levels() == 2u);
    CHECK(expedition.summaries().size() == 2u);
}

TEST_CASE("completing a level before its objective finishes is an explicit no-op") {
    Expedition expedition(kSeed);
    REQUIRE_FALSE(expedition.state().objective_completed());

    CHECK(expedition.complete_level(LevelPerformance{10u, 1u}) == LevelTransition::none);
    CHECK(expedition.completed_levels() == 0u);
    CHECK(expedition.total_score() == 0u);
    CHECK(expedition.summaries().empty());
}

TEST_CASE("score and discoveries accumulate across the whole expedition") {
    Expedition expedition(kSeed);
    REQUIRE(finish_level(expedition.state()));
    REQUIRE(expedition.complete_level(LevelPerformance{30u, 0u}) == LevelTransition::advanced);

    const std::uint64_t after_first = expedition.total_score();
    const std::uint32_t available_first = expedition.total_discoveries_available();
    REQUIRE(after_first == expedition.summaries()[0].score.value);
    REQUIRE(available_first > 0u);

    REQUIRE(finish_level(expedition.state()));
    REQUIRE(expedition.complete_level(LevelPerformance{30u, 0u}) ==
            LevelTransition::expedition_completed);

    CHECK(expedition.total_score() ==
          after_first + expedition.summaries()[1].score.value);
    CHECK(expedition.total_discoveries_available() > available_first);
    CHECK(expedition.total_discoveries_found() ==
          expedition.summaries()[0].discoveries_found +
              expedition.summaries()[1].discoveries_found);
}

TEST_CASE("finding every discovery earns the bonus that doubles the next level's discoveries") {
    Expedition expedition(kSeed);
    REQUIRE(finish_level_collecting_everything(expedition.state()));
    REQUIRE(expedition.state().discoveries_found() == expedition.state().discovery_total());
    REQUIRE(expedition.state().discovery_total() > 0u);

    REQUIRE(expedition.complete_level(LevelPerformance{}) == LevelTransition::advanced);
    CHECK(expedition.summaries()[0].applied_bonus == ExpeditionBonus::none);
    CHECK(expedition.summaries()[0].earned_bonus == ExpeditionBonus::keen_eye);
    CHECK(expedition.active_bonus() == ExpeditionBonus::keen_eye);

    REQUIRE(finish_level_collecting_everything(expedition.state()));
    REQUIRE(expedition.complete_level(LevelPerformance{}) ==
            LevelTransition::expedition_completed);

    const LevelSummary& second = expedition.summaries()[1];
    CHECK(second.applied_bonus == ExpeditionBonus::keen_eye);
    CHECK(second.score.discovery_multiplier == 2u);
    CHECK(second.score.discovery_value ==
          second.discoveries_found * completed_score_per_discovery * 2u);
}

TEST_CASE("missing a discovery loses the carried bonus") {
    Expedition expedition(kSeed);
    REQUIRE(finish_level(expedition.state()));
    REQUIRE(expedition.state().discoveries_found() < expedition.state().discovery_total());

    REQUIRE(expedition.complete_level(LevelPerformance{}) == LevelTransition::advanced);
    CHECK(expedition.summaries()[0].earned_bonus == ExpeditionBonus::none);
    CHECK(expedition.active_bonus() == ExpeditionBonus::none);
}

TEST_CASE("the same seed replays the same expedition exactly") {
    Expedition first(kSeed);
    Expedition second(kSeed);

    REQUIRE(finish_level(first.state()));
    REQUIRE(finish_level(second.state()));
    REQUIRE(first.complete_level(LevelPerformance{25u, 2u}) == LevelTransition::advanced);
    REQUIRE(second.complete_level(LevelPerformance{25u, 2u}) == LevelTransition::advanced);

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
}

TEST_CASE("make_level_state builds a playable level for every tier") {
    for (const LevelTier tier : {LevelTier::small, LevelTier::medium, LevelTier::large,
                                 LevelTier::x_large}) {
        const GameState state = make_level_state(tier, kSeed);
        CHECK(state.map().width() == dimensions_of(tier).width);
        CHECK(state.map().height() == dimensions_of(tier).height);
        CHECK(state.actor_position() == center_spawn_of(tier));
        CHECK(state.stamina() == GameState::maximum_stamina);
        CHECK(state.discovery_total() > 0u);
    }
}
