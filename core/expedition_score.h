#pragma once

#include <cstdint>

// Frontend-neutral, deterministic expedition scoring. Score rules and the
// optimal route baseline live in the core so every frontend agrees on the score
// of a run (CON-102). Every function performs only fixed-width unsigned
// arithmetic with comparison-before-subtraction and saturating accumulation, so
// it can never underflow or overflow before the final clamp. None of it touches
// a terminal, prose, clock, environment, or mutable global state.

// The typed inputs to the level score. `optimal_route_cost` is the objective's
// cheapest spawn-to-landmark-to-exit stamina cost and `actual_stamina_spent` is
// what the run actually spent, so an efficient route scores higher than a
// wandering one. Blocked attempts are the moves the map refused.
struct CompletedScoreInput {
    std::uint64_t optimal_route_cost = 0;
    std::uint64_t actual_stamina_spent = 0;
    std::uint64_t blocked_attempts = 0;
};

// The transparent score breakdown. `value` is the final clamped score; the
// remaining fields expose every quantity a report presents so a frontend never
// re-derives score math.
struct ExpeditionScore {
    std::uint64_t value = 0;  // Final score, clamped to the maximum.

    std::uint64_t optimal_route_cost = 0;
    std::uint64_t actual_stamina_spent = 0;
    std::uint64_t excess_stamina = 0;  // max(0, actual - optimal).
    std::uint64_t blocked_attempts = 0;
};

// Level scoring constants.
inline constexpr std::uint64_t completed_score_base = 1000;
inline constexpr std::uint64_t completed_score_maximum = 1000;
inline constexpr std::uint64_t completed_penalty_per_excess_stamina = 5;
inline constexpr std::uint64_t completed_penalty_per_blocked_attempt = 20;

// Compute the deterministic score for a completed level: start at
// completed_score_base, subtract completed_penalty_per_excess_stamina per stamina
// point above the optimal route, subtract completed_penalty_per_blocked_attempt
// per blocked move, and clamp into [0, completed_score_maximum]. Overflow-safe.
[[nodiscard]] ExpeditionScore compute_completed_score(const CompletedScoreInput& input) noexcept;
