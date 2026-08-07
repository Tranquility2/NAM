#include "expedition.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include "exploration.h"

GameState make_level_state(LevelTier tier, std::uint64_t numeric_seed, int vantage_reveal_bonus) {
    WorldGenerationResult result = generate_level(tier, numeric_seed);
    if (!std::holds_alternative<GeneratedWorld>(result)) {
        const WorldGenerationError error = std::get<WorldGenerationError>(result);
        throw std::runtime_error(std::string("level generation failed: ") +
                                 std::string(to_string(error.code)));
    }
    return GameState(std::move(std::get<GeneratedWorld>(result).map), vantage_reveal_bonus);
}

bool earned_bonus_test(ExpeditionBonus bonus, const LevelSummary& summary) noexcept {
    switch (bonus) {
        case ExpeditionBonus::none:
            return false;
        case ExpeditionBonus::keen_eye:
            // A clean sweep: nearly all of the level uncovered *and* all of its
            // optional content entered. A level that placed no discoveries cannot
            // be swept, so it earns nothing.
            return summary.discovery_total > 0u &&
                   summary.discoveries_found == summary.discovery_total &&
                   summary.score.explored_percent >= keen_eye_explored_percent;
        case ExpeditionBonus::surveyor:
            // Every viewpoint the level offered was stood on.
            return summary.vantage_total > 0u &&
                   summary.vantages_reached == summary.vantage_total;
        case ExpeditionBonus::pathfinder:
            // The exit reached inside par. A level with no par to beat — a map so
            // small that a quarter of its budget rounds to nothing — cannot be
            // walked efficiently, so it earns nothing.
            return summary.score.par_moves > 0u &&
                   summary.score.actual_moves <= summary.score.par_moves;
    }
    return false;
}

ExpeditionBonus earned_bonus_of(const LevelSummary& summary) noexcept {
    for (const ExpeditionBonus bonus : earnable_bonuses) {
        if (earned_bonus_test(bonus, summary)) {
            return bonus;
        }
    }
    return ExpeditionBonus::none;
}

Expedition::Expedition(std::uint64_t numeric_seed, LevelTier final_tier)
    : numeric_seed_(numeric_seed),
      progress_(final_tier),
      state_(make_level_state(progress_.current_tier(), numeric_seed)) {}

Expedition::Expedition(GameState state, std::uint64_t numeric_seed)
    : numeric_seed_(numeric_seed),
      progress_(LevelTier::small),
      state_(std::move(state)) {}

LevelTransition Expedition::complete_level(const LevelPerformance& performance) {
    if (progress_.completed() || !state_.objective_completed()) {
        return LevelTransition::none;
    }

    // The exploration statistic is core-owned: the numerator comes from the
    // level's own fog snapshot and the denominator from its objective, so no
    // frontend can disagree with the score about how much was uncovered.
    CompletedScoreInput input;
    input.explored_reachable_cells =
        count_explored_reachable_walkable_cells(state_.map(), state_.visibility());
    input.total_reachable_cells = state_.objective().total_reachable_walkable_cells;
    input.actual_moves = performance.moves_taken;
    input.discoveries_found = state_.discoveries_found();
    input.discovery_multiplier = discovery_multiplier_of(active_bonus_);
    input.budget_multiplier = budget_multiplier_of(active_bonus_);

    LevelSummary summary;
    summary.tier = progress_.current_tier();
    summary.score = compute_completed_score(input);
    summary.discoveries_found = state_.discoveries_found();
    summary.discovery_total = state_.discovery_total();
    summary.vantages_reached = state_.vantages_reached();
    summary.vantage_total = state_.vantage_total();
    summary.applied_bonus = active_bonus_;
    // Exactly one bonus carries forward, chosen hardest first, so the reward for
    // a level the player did everything on is still a single line to read.
    summary.earned_bonus = earned_bonus_of(summary);

    total_score_ += summary.score.value;
    total_discoveries_found_ += summary.discoveries_found;
    total_discoveries_available_ += summary.discovery_total;
    active_bonus_ = summary.earned_bonus;
    summaries_.push_back(summary);

    const LevelTransition transition = progress_.complete_current_level();
    if (transition == LevelTransition::advanced) {
        // The tier owns its own RNG stream, so one run seed already yields
        // unrelated levels per tier; no per-level seed mixing is needed. The
        // carried bonus reaches into the new level only as extra vantage reach.
        state_ = make_level_state(progress_.current_tier(), numeric_seed_,
                                  vantage_reveal_bonus_of(active_bonus_));
    }
    return transition;
}
