#pragma once

#include <cstdint>

// Frontend-neutral, deterministic expedition scoring. Score rules live in the
// core so every frontend agrees on the score of a run (CON-102). Every function
// performs only fixed-width unsigned arithmetic with comparison-before-
// subtraction and saturating accumulation, so it can never underflow or overflow
// before the final clamp. None of it touches a terminal, prose, clock,
// environment, or mutable global state.

// The typed inputs to the level score.
//
// `explored_reachable_cells` out of `total_reachable_cells` is the exploration
// statistic: how much of the level the run actually uncovered, measured over the
// walkable cells reachable from spawn. `actual_moves` is how many moves the run
// took, weighed only against a deliberately generous budget derived from the
// level's own size. `discoveries_found` is the optional content the run entered,
// and `discovery_multiplier` is the carried bonus applied to it (1 when no bonus
// is active). A multiplier of 0 is treated as 1 so a default-constructed input
// still scores discoveries.
//
// There is no shortest-route input and no blocked-attempt input. The score no
// longer measures how closely a run tracked the direct line, because measuring
// that punished the one thing the game asks the player to do.
struct CompletedScoreInput {
    std::uint64_t explored_reachable_cells = 0;
    std::uint64_t total_reachable_cells = 0;
    std::uint64_t actual_moves = 0;
    std::uint64_t discoveries_found = 0;
    std::uint64_t discovery_multiplier = 1;
};

// The transparent score breakdown. `value` is the sum of the four components;
// the remaining fields expose every quantity a report presents so a frontend
// never re-derives score math.
struct ExpeditionScore {
    std::uint64_t value = 0;

    // Reaching the exit. A flat award, so finishing levels is always the largest
    // single contribution to a run.
    std::uint64_t completion_value = 0;

    // How much of the level was uncovered.
    std::uint64_t explored_reachable_cells = 0;
    std::uint64_t total_reachable_cells = 0;
    std::uint64_t explored_percent = 0;  // Floored 0..100, for display.
    std::uint64_t exploration_value = 0;

    // Optional content entered, and the carried bonus applied to it.
    std::uint64_t discoveries_found = 0;
    std::uint64_t discovery_multiplier = 1;
    std::uint64_t discovery_value = 0;

    // The soft move budget.
    std::uint64_t actual_moves = 0;
    std::uint64_t move_budget = 0;
    std::uint64_t moves_over_budget = 0;
    std::uint64_t budget_value = 0;
};

// Level scoring constants, in the priority order the design calls for: reaching
// the exit, then uncovering the level, then optional discoveries, then beating
// the move budget.
//
// The awards are ordered so the arithmetic says the same thing the prose does.
// The exit is worth more than a perfect sweep, so a run always finishes levels;
// a perfect sweep is worth four discoveries, so exploring beats collecting; and
// the entire budget bucket is worth a quarter of a sweep, so no amount of
// efficiency can out-earn looking around.
inline constexpr std::uint64_t completed_exit_award = 1000;
inline constexpr std::uint64_t completed_exploration_maximum = 800;
inline constexpr std::uint64_t completed_score_per_discovery = 200;
inline constexpr std::uint64_t completed_budget_award = 200;
inline constexpr std::uint64_t completed_penalty_per_move_over_budget = 5;

// The soft move budget for a level: one move per walkable cell reachable from
// spawn. Measured over generated levels, a greedy full sweep costs roughly a
// third of that, so a player who uncovers the entire level still finishes well
// inside the budget with room to backtrack, re-cross, and change their mind.
//
// That margin is what makes the design's central invariant structural rather
// than lucky: a longer route that uncovers more map only begins losing budget
// points long after it has run out of exploration points to gain, and the whole
// budget bucket is worth less than a quarter of the exploration one.
[[nodiscard]] constexpr std::uint64_t move_budget_for(std::uint64_t total_reachable_cells) noexcept {
    return total_reachable_cells;
}

// Compute the deterministic score for a completed level:
//   * completed_exit_award for reaching the exit at all;
//   * completed_exploration_maximum scaled linearly by the explored fraction;
//   * completed_score_per_discovery per discovery, times the carried multiplier;
//   * completed_budget_award less completed_penalty_per_move_over_budget for
//     every move past the budget, floored at zero.
// Overflow-safe: a hostile counter saturates instead of wrapping.
[[nodiscard]] ExpeditionScore compute_completed_score(const CompletedScoreInput& input) noexcept;
