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
// returned to spawn; a rescued run ran out of provisions and was rescued; an
// overdue run missed its return deadline and was collected late.
enum class ExpeditionResult {
    completed,
    rescued,
    overdue,
};

// The typed inputs to the completed-run score. `provisions_remaining` is the
// stock left at completion; the one spare provision counts as unused when at
// least one provision remains. `days_used` is the numbered day the run completed
// on and `minimum_completion_days` is the map-derived baseline; each day beyond
// the baseline subtracts a fixed penalty (REQ-038).
struct CompletedScoreInput {
    std::uint64_t optimal_round_trip_cost = 0;
    std::uint64_t actual_stamina_spent = 0;
    std::uint64_t blocked_attempts = 0;
    std::uint64_t provisions_remaining = 0;
    std::uint64_t days_used = 1;
    std::uint64_t minimum_completion_days = 1;
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

// The typed inputs to the overdue-run score. Identical in shape to the rescued
// inputs; only the beacon bonus and the clamp differ (REQ-040).
struct OverdueScoreInput {
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
    std::uint64_t days_used = 1;
    std::uint64_t minimum_completion_days = 1;
    std::uint64_t excess_days = 0;  // max(0, days_used - minimum_completion_days).

    // Rescued- and overdue-run breakdown.
    std::uint64_t explored_reachable_cells = 0;
    std::uint64_t total_reachable_cells = 0;
    std::uint64_t exploration_points = 0;  // floor(500 * explored / total).
    bool beacon_discovered = false;

    // Shared.
    std::uint64_t blocked_attempts = 0;
};

// Completed-run scoring constants (REQ-038).
inline constexpr std::uint64_t completed_score_base = 900;
inline constexpr std::uint64_t completed_score_maximum = 1000;
inline constexpr std::uint64_t completed_spare_bonus = 100;
inline constexpr std::uint64_t completed_penalty_per_excess_stamina = 5;
inline constexpr std::uint64_t completed_penalty_per_blocked_attempt = 20;
inline constexpr std::uint64_t completed_penalty_per_excess_day = 25;

// Rescued-run scoring constants (REQ-039).
inline constexpr std::uint64_t rescued_score_maximum = 750;
inline constexpr std::uint64_t rescued_exploration_maximum = 500;
inline constexpr std::uint64_t rescued_beacon_bonus = 250;
inline constexpr std::uint64_t rescued_penalty_per_blocked_attempt = 20;

// Overdue-run scoring constants (REQ-040).
inline constexpr std::uint64_t overdue_score_maximum = 600;
inline constexpr std::uint64_t overdue_exploration_maximum = 500;
inline constexpr std::uint64_t overdue_beacon_bonus = 100;
inline constexpr std::uint64_t overdue_penalty_per_blocked_attempt = 20;

// Compute the deterministic score for a completed expedition: start at
// completed_score_base, add completed_spare_bonus when the spare provision is
// unused, subtract completed_penalty_per_excess_stamina per stamina point above
// the optimal round trip, completed_penalty_per_blocked_attempt per blocked move,
// and completed_penalty_per_excess_day per day used beyond the minimum completion
// day count, and clamp into [0, completed_score_maximum]. Overflow-safe (REQ-038).
[[nodiscard]] ExpeditionScore compute_completed_score(const CompletedScoreInput& input) noexcept;

// Compute the deterministic score for a rescued expedition: an exploration
// component floor(rescued_exploration_maximum * explored / total) in
// [0, rescued_exploration_maximum], plus rescued_beacon_bonus when the beacon was
// discovered, minus rescued_penalty_per_blocked_attempt per blocked move, clamped
// into [0, rescued_score_maximum]. Overflow-safe (REQ-039).
[[nodiscard]] ExpeditionScore compute_rescued_score(const RescuedScoreInput& input) noexcept;

// Compute the deterministic score for an overdue expedition: an exploration
// component floor(overdue_exploration_maximum * explored / total) in
// [0, overdue_exploration_maximum], plus overdue_beacon_bonus when the beacon was
// discovered, minus overdue_penalty_per_blocked_attempt per blocked move, clamped
// into [0, overdue_score_maximum]. Overflow-safe (REQ-040).
[[nodiscard]] ExpeditionScore compute_overdue_score(const OverdueScoreInput& input) noexcept;
