#include "expedition_score.h"

namespace {

// max(0, left - right) for unsigned values via comparison-before-subtraction, so
// the subtraction never underflows.
[[nodiscard]] std::uint64_t saturating_sub(std::uint64_t left, std::uint64_t right) noexcept {
    return left > right ? left - right : 0;
}

// left * right saturated to uint64 max, so a hostile counter can never overflow a
// running penalty total before the final clamp (RISK-101).
[[nodiscard]] std::uint64_t saturating_mul(std::uint64_t left, std::uint64_t right) noexcept {
    if (left == 0 || right == 0) {
        return 0;
    }
    constexpr std::uint64_t maximum = static_cast<std::uint64_t>(-1);
    if (left > maximum / right) {
        return maximum;
    }
    return left * right;
}

// left + right saturated to uint64 max.
[[nodiscard]] std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) noexcept {
    constexpr std::uint64_t maximum = static_cast<std::uint64_t>(-1);
    if (left > maximum - right) {
        return maximum;
    }
    return left + right;
}

// min(value, ceiling): a plain clamp of an upper bound.
[[nodiscard]] std::uint64_t at_most(std::uint64_t value, std::uint64_t ceiling) noexcept {
    return value < ceiling ? value : ceiling;
}

}  // namespace

ExpeditionScore compute_completed_score(const CompletedScoreInput& input) noexcept {
    ExpeditionScore score;

    // Reaching the exit is what calling this function means, so the award is
    // unconditional here rather than gated on a flag the caller could forget.
    score.completion_value = completed_exit_award;

    // An explored count above the total would be a caller bug; clamping keeps a
    // wrong count from inflating the fraction past 100%.
    score.total_reachable_cells = input.total_reachable_cells;
    score.explored_reachable_cells =
        at_most(input.explored_reachable_cells, input.total_reachable_cells);

    // A level with no reachable cells cannot be explored, so it scores nothing
    // here rather than dividing by zero. Scale before dividing so the linear
    // fraction keeps its resolution on small levels.
    if (score.total_reachable_cells > 0) {
        score.explored_percent = at_most(
            saturating_mul(score.explored_reachable_cells, 100) / score.total_reachable_cells, 100);
        score.exploration_value =
            at_most(saturating_mul(score.explored_reachable_cells, completed_exploration_maximum) /
                        score.total_reachable_cells,
                    completed_exploration_maximum);
    }

    // A zero multiplier would silently erase the exploration reward, so treat it
    // as the neutral value rather than trusting the caller.
    score.discoveries_found = input.discoveries_found;
    score.discovery_multiplier = input.discovery_multiplier == 0 ? 1 : input.discovery_multiplier;
    score.discovery_value =
        saturating_mul(saturating_mul(score.discoveries_found, completed_score_per_discovery),
                       score.discovery_multiplier);

    // The budget is soft: overspending empties this one bucket and can never
    // reach into the three earned above it. A carried multiplier scales only what
    // survives the overspend, so a bonus rewards efficiency rather than excusing
    // its absence.
    score.actual_moves = input.actual_moves;
    score.move_budget = move_budget_for(score.total_reachable_cells);
    score.par_moves = par_moves_for(score.total_reachable_cells);
    score.moves_over_budget = saturating_sub(score.actual_moves, score.move_budget);
    score.budget_multiplier = input.budget_multiplier == 0 ? 1 : input.budget_multiplier;
    score.budget_value = saturating_mul(
        saturating_sub(
            completed_budget_award,
            saturating_mul(score.moves_over_budget, completed_penalty_per_move_over_budget)),
        score.budget_multiplier);

    score.value = saturating_add(score.completion_value, score.exploration_value);
    score.value = saturating_add(score.value, score.discovery_value);
    score.value = saturating_add(score.value, score.budget_value);
    return score;
}
