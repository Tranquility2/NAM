#include "expedition.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

GameState make_level_state(LevelTier tier, std::uint64_t numeric_seed) {
    WorldGenerationResult result = generate_level(tier, numeric_seed);
    if (!std::holds_alternative<GeneratedWorld>(result)) {
        const WorldGenerationError error = std::get<WorldGenerationError>(result);
        throw std::runtime_error(std::string("level generation failed: ") +
                                 std::string(to_string(error.code)));
    }
    return GameState(std::move(std::get<GeneratedWorld>(result).map));
}

Expedition::Expedition(std::uint64_t numeric_seed, LevelTier final_tier)
    : numeric_seed_(numeric_seed),
      progress_(final_tier),
      state_(make_level_state(progress_.current_tier(), numeric_seed)) {}

LevelTransition Expedition::complete_level(const LevelPerformance& performance) {
    if (progress_.completed() || !state_.objective_completed()) {
        return LevelTransition::none;
    }

    CompletedScoreInput input;
    input.optimal_route_cost = state_.objective().minimum_route_stamina_cost;
    input.actual_stamina_spent = performance.stamina_spent;
    input.blocked_attempts = performance.blocked_attempts;
    input.discoveries_found = state_.discoveries_found();
    input.discovery_multiplier = discovery_multiplier_of(active_bonus_);

    LevelSummary summary;
    summary.tier = progress_.current_tier();
    summary.score = compute_completed_score(input);
    summary.discoveries_found = state_.discoveries_found();
    summary.discovery_total = state_.discovery_total();
    summary.applied_bonus = active_bonus_;
    // A clean sweep of the level's optional content earns the one carried bonus.
    // A level that placed no discoveries cannot be swept, so it earns nothing.
    summary.earned_bonus =
        (summary.discovery_total > 0u && summary.discoveries_found == summary.discovery_total)
            ? ExpeditionBonus::keen_eye
            : ExpeditionBonus::none;

    total_score_ += summary.score.value;
    total_discoveries_found_ += summary.discoveries_found;
    total_discoveries_available_ += summary.discovery_total;
    active_bonus_ = summary.earned_bonus;
    summaries_.push_back(summary);

    const LevelTransition transition = progress_.complete_current_level();
    if (transition == LevelTransition::advanced) {
        // The tier owns its own RNG stream, so one run seed already yields
        // unrelated levels per tier; no per-level seed mixing is needed.
        state_ = make_level_state(progress_.current_tier(), numeric_seed_);
    }
    return transition;
}
