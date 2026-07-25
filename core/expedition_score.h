#pragma once

#include <cstdint>

// Frontend-neutral, deterministic expedition scoring. Score rules and the
// optimal round-trip baseline live in the core so every frontend agrees on the
// score of a run (CON-102). Every function performs only fixed-width unsigned
// arithmetic with comparison-before-subtraction and saturating accumulation, so
// it can never underflow or overflow before the final clamp (REQ-140 / REQ-141).
// None of it touches a terminal, prose, clock, environment, or mutable global
// state.

// Which ending a scored run reached. A completed run reached the beacon and
// returned to spawn; a rescued run ran out of provisions and was rescued.
enum class ExpeditionResult {
    completed,
    rescued,
};

// The typed inputs to the completed-run score. `provisions_remaining` is the
// stock left at completion; the one spare provision counts as unused when at
// least one provision remains.
struct CompletedScoreInput {
    std::uint64_t optimal_round_trip_cost = 0;
    std::uint64_t actual_stamina_spent = 0;
    std::uint64_t blocked_attempts = 0;
    std::uint64_t provisions_remaining = 0;
};

// The typed inputs to the rescued-run score. The exploration fraction is
// explored / total reachable walkable cells; `beacon_discovered` is whether the
// beacon was reached before the rescue.
struct RescuedScoreInput {
    std::uint64_t explored_reachable_cells = 0;
    std::uint64_t total_reachable_cells = 0;
    bool beacon_discovered = false;
    std::uint64_t blocked_attempts = 0;
};

// The transparent score breakdown for either ending. `value` is the final clamped
// score; the remaining fields expose every quantity a report presents so a
// frontend never re-derives score math.
struct ExpeditionScore {
    ExpeditionResult result = ExpeditionResult::completed;
    std::uint64_t value = 0;  // Final score, clamped to the result's maximum.

    // Completed-run breakdown.
    std::uint64_t optimal_round_trip_cost = 0;
    std::uint64_t actual_stamina_spent = 0;
    std::uint64_t excess_stamina = 0;  // max(0, actual - optimal).
    bool spare_unused = false;         // The one spare provision remained.

    // Rescued-run breakdown.
    std::uint64_t explored_reachable_cells = 0;
    std::uint64_t total_reachable_cells = 0;
    std::uint64_t exploration_points = 0;  // floor(500 * explored / total).
    bool beacon_discovered = false;

    // Shared.
    std::uint64_t blocked_attempts = 0;
};

// Completed-run scoring constants (REQ-140).
inline constexpr std::uint64_t completed_score_base = 900;
inline constexpr std::uint64_t completed_score_maximum = 1000;
inline constexpr std::uint64_t completed_spare_bonus = 100;
inline constexpr std::uint64_t completed_penalty_per_excess_stamina = 5;
inline constexpr std::uint64_t completed_penalty_per_blocked_attempt = 20;

// Rescued-run scoring constants (REQ-141).
inline constexpr std::uint64_t rescued_score_maximum = 750;
inline constexpr std::uint64_t rescued_exploration_maximum = 500;
inline constexpr std::uint64_t rescued_beacon_bonus = 250;
inline constexpr std::uint64_t rescued_penalty_per_blocked_attempt = 20;

// Compute the deterministic score for a completed expedition: start at
// completed_score_base, add completed_spare_bonus when the spare provision is
// unused, subtract completed_penalty_per_excess_stamina per stamina point above
// the optimal round trip and completed_penalty_per_blocked_attempt per blocked
// move, and clamp into [0, completed_score_maximum]. Overflow-safe (REQ-140).
[[nodiscard]] ExpeditionScore compute_completed_score(const CompletedScoreInput& input) noexcept;

// Compute the deterministic score for a rescued expedition: an exploration
// component floor(rescued_exploration_maximum * explored / total) in
// [0, rescued_exploration_maximum], plus rescued_beacon_bonus when the beacon was
// discovered, minus rescued_penalty_per_blocked_attempt per blocked move, clamped
// into [0, rescued_score_maximum]. Overflow-safe (REQ-141).
[[nodiscard]] ExpeditionScore compute_rescued_score(const RescuedScoreInput& input) noexcept;
