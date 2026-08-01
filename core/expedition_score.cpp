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
    score.optimal_route_length = input.optimal_route_length;
    score.actual_moves = input.actual_moves;
    score.blocked_attempts = input.blocked_attempts;

    // Excess moves are every move taken beyond the shortest legal route.
    score.excess_moves = saturating_sub(input.actual_moves, input.optimal_route_length);

    // Accumulate penalties with saturating arithmetic, then subtract with
    // comparison-before-subtraction.
    std::uint64_t penalty = 0;
    penalty = saturating_add(
        penalty, saturating_mul(score.excess_moves, completed_penalty_per_excess_move));
    penalty = saturating_add(
        penalty, saturating_mul(score.blocked_attempts, completed_penalty_per_blocked_attempt));

    score.route_value =
        at_most(saturating_sub(completed_score_base, penalty), completed_score_maximum);

    // A zero multiplier would silently erase the exploration reward, so treat it
    // as the neutral value rather than trusting the caller.
    score.discoveries_found = input.discoveries_found;
    score.discovery_multiplier = input.discovery_multiplier == 0 ? 1 : input.discovery_multiplier;
    score.discovery_value =
        saturating_mul(saturating_mul(score.discoveries_found, completed_score_per_discovery),
                       score.discovery_multiplier);

    score.value = saturating_add(score.route_value, score.discovery_value);
    return score;
}
