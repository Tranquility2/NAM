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
// `discoveries_found` is the optional content the run actually entered, and
// `discovery_multiplier` is the carried bonus applied to it (1 when no bonus is
// active). A multiplier of 0 is treated as 1 so a default-constructed input still
// scores discoveries.
struct CompletedScoreInput {
    std::uint64_t optimal_route_cost = 0;
    std::uint64_t actual_stamina_spent = 0;
    std::uint64_t blocked_attempts = 0;
    std::uint64_t discoveries_found = 0;
    std::uint64_t discovery_multiplier = 1;
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

    // The route half of the score: the base minus penalties, clamped. Reported
    // separately so a frontend can show what efficiency and exploration each
    // contributed without re-deriving either.
    std::uint64_t route_value = 0;
    std::uint64_t discoveries_found = 0;
    std::uint64_t discovery_multiplier = 1;
    std::uint64_t discovery_value = 0;
};

// Level scoring constants. The route half is bounded by completed_score_maximum;
// the discovery half is added on top of that bound on purpose, so exploring is
// always worth more than the detour it costs. One discovery is worth 200, while
// 40 wasted stamina points is worth 200 in penalties, and no discovery detour in
// a prototype tier is anywhere near 40 stamina. That ordering is what makes
// "exploration is rewarded, not required" true in the arithmetic rather than only
// in the prose.
inline constexpr std::uint64_t completed_score_base = 1000;
inline constexpr std::uint64_t completed_score_maximum = 1000;
inline constexpr std::uint64_t completed_penalty_per_excess_stamina = 5;
inline constexpr std::uint64_t completed_penalty_per_blocked_attempt = 20;
inline constexpr std::uint64_t completed_score_per_discovery = 200;

// Compute the deterministic score for a completed level: start at
// completed_score_base, subtract completed_penalty_per_excess_stamina per stamina
// point above the optimal route, subtract completed_penalty_per_blocked_attempt
// per blocked move, and clamp that route half into [0, completed_score_maximum].
// Then add completed_score_per_discovery for every discovery entered, multiplied
// by the carried discovery multiplier. Overflow-safe.
[[nodiscard]] ExpeditionScore compute_completed_score(const CompletedScoreInput& input) noexcept;
