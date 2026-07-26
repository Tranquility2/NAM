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
    score.result = ExpeditionResult::completed;
    score.optimal_round_trip_cost = input.optimal_round_trip_cost;
    score.actual_stamina_spent = input.actual_stamina_spent;
    score.blocked_attempts = input.blocked_attempts;

    // Excess stamina is every actual stamina point beyond the cheapest round trip.
    score.excess_stamina =
        saturating_sub(input.actual_stamina_spent, input.optimal_round_trip_cost);
    // The one spare provision is unused when at least one provision remains.
    score.spare_unused = input.provisions_remaining >= 1;
    // Excess days are every numbered day used beyond the minimum completion day
    // count (REQ-038).
    score.days_used = input.days_used;
    score.minimum_completion_days = input.minimum_completion_days;
    score.excess_days = saturating_sub(input.days_used, input.minimum_completion_days);

    // Start at the base and add the spare bonus, saturating so the reward can never
    // overflow before the clamp.
    std::uint64_t reward = completed_score_base;
    if (score.spare_unused) {
        reward = saturating_add(reward, completed_spare_bonus);
    }

    // Accumulate penalties with saturating arithmetic, then subtract with
    // comparison-before-subtraction.
    std::uint64_t penalty = 0;
    penalty = saturating_add(
        penalty, saturating_mul(score.excess_stamina, completed_penalty_per_excess_stamina));
    penalty = saturating_add(
        penalty, saturating_mul(score.blocked_attempts, completed_penalty_per_blocked_attempt));
    penalty = saturating_add(
        penalty, saturating_mul(score.excess_days, completed_penalty_per_excess_day));

    score.value = at_most(saturating_sub(reward, penalty), completed_score_maximum);
    return score;
}

ExpeditionScore compute_rescued_score(const RescuedScoreInput& input) noexcept {
    ExpeditionScore score;
    score.result = ExpeditionResult::rescued;
    score.explored_reachable_cells = input.explored_reachable_cells;
    score.total_reachable_cells = input.total_reachable_cells;
    score.beacon_discovered = input.beacon_discovered;
    score.blocked_attempts = input.blocked_attempts;

    // Exploration component: floor(max * explored / total), clamped so a defensive
    // explored > total can never exceed the maximum, and guarded against a zero
    // denominator. explored is bounded by the map size, so the multiply cannot
    // overflow, but saturating_mul keeps it safe regardless.
    if (input.total_reachable_cells > 0) {
        const std::uint64_t numerator =
            saturating_mul(rescued_exploration_maximum, input.explored_reachable_cells);
        score.exploration_points =
            at_most(numerator / input.total_reachable_cells, rescued_exploration_maximum);
    }

    std::uint64_t reward = score.exploration_points;
    if (input.beacon_discovered) {
        reward = saturating_add(reward, rescued_beacon_bonus);
    }
    const std::uint64_t penalty =
        saturating_mul(input.blocked_attempts, rescued_penalty_per_blocked_attempt);

    score.value = at_most(saturating_sub(reward, penalty), rescued_score_maximum);
    return score;
}

ExpeditionScore compute_overdue_score(const OverdueScoreInput& input) noexcept {
    ExpeditionScore score;
    score.result = ExpeditionResult::overdue;
    score.explored_reachable_cells = input.explored_reachable_cells;
    score.total_reachable_cells = input.total_reachable_cells;
    score.beacon_discovered = input.beacon_discovered;
    score.blocked_attempts = input.blocked_attempts;

    // Exploration component: floor(max * explored / total), clamped and guarded
    // against a zero denominator exactly like the rescued score.
    if (input.total_reachable_cells > 0) {
        const std::uint64_t numerator =
            saturating_mul(overdue_exploration_maximum, input.explored_reachable_cells);
        score.exploration_points =
            at_most(numerator / input.total_reachable_cells, overdue_exploration_maximum);
    }

    std::uint64_t reward = score.exploration_points;
    if (input.beacon_discovered) {
        reward = saturating_add(reward, overdue_beacon_bonus);
    }
    const std::uint64_t penalty =
        saturating_mul(input.blocked_attempts, overdue_penalty_per_blocked_attempt);

    score.value = at_most(saturating_sub(reward, penalty), overdue_score_maximum);
    return score;
}
