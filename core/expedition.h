#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "expedition_progress.h"
#include "expedition_score.h"
#include "game_state.h"
#include "level_tier.h"
#include "world_generation.h"

// One complete expedition: the chain of levels a single seed produces, plus the
// only state that carries between them. Everything here is frontend-neutral and
// deterministic — the same seed always yields the same levels, the same scores,
// and the same bonuses.
//
// Carryover is deliberately narrow (roadmap section 3): the shared score, the
// discovery tally, one simple bonus, the current tier, and the run identity.
// Nothing else survives a level, because each level owns a fresh GameState.

// The one simple carried bonus the prototype grants. It must stay immediately
// understandable and must never grow into an inventory.
enum class ExpeditionBonus {
    none,
    // Earned by sweeping a level: uncovering at least keen_eye_explored_percent
    // of it *and* entering every discovery on it. The next level's discoveries
    // are worth double.
    keen_eye,
};

// The share of a level that must be uncovered to earn keen_eye. Finding every
// discovery is not enough on its own: a generated level places a single vantage
// point, so a discovery sweep alone is a short walk rather than an achievement.
// Pairing it with an exploration threshold makes the bonus mean what its name
// says.
inline constexpr std::uint64_t keen_eye_explored_percent = 90;

// The multiplier a bonus applies to the next level's discovery score.
[[nodiscard]] constexpr std::uint64_t discovery_multiplier_of(ExpeditionBonus bonus) noexcept {
    switch (bonus) {
        case ExpeditionBonus::none:     return 1;
        case ExpeditionBonus::keen_eye: return 2;
    }
    return 1;
}

// A stable, non-localized identifier. Frontends choose the user-facing wording.
[[nodiscard]] constexpr std::string_view to_string(ExpeditionBonus bonus) noexcept {
    switch (bonus) {
        case ExpeditionBonus::none:     return "none";
        case ExpeditionBonus::keen_eye: return "keen_eye";
    }
    return "none";
}

// What one finished level contributed to the expedition. Recorded in completion
// order so a final report can present the run without replaying it.
struct LevelSummary {
    LevelTier tier = LevelTier::small;
    ExpeditionScore score{};
    std::uint32_t discoveries_found = 0;
    std::uint32_t discovery_total = 0;
    // The bonus that was active while this level was played.
    ExpeditionBonus applied_bonus = ExpeditionBonus::none;
    // The bonus this level earned for the next one.
    ExpeditionBonus earned_bonus = ExpeditionBonus::none;
};

// What the frontend measured over one level. The core does not observe input, so
// the counters a run accumulates are handed back at completion time. Only the
// move count is scored; blocked attempts cost a run nothing, because bumping
// into a cliff is how a player learns where the cliff is.
struct LevelPerformance {
    std::uint64_t moves_taken = 0;
};

// The prototype plays Small then Medium. Phase 2 extends this to x_large.
inline constexpr LevelTier prototype_final_tier = LevelTier::medium;

class Expedition {
public:
    // Build the expedition's first level from a run seed. Generation is expected
    // to succeed for every tier; a seed that exhausts the candidate limit throws,
    // which callers avoid by generating through generate_level first.
    explicit Expedition(std::uint64_t numeric_seed, LevelTier final_tier = prototype_final_tier);

    // A one-level expedition around an already-built level. Handcrafted maps and
    // the built-in map are not part of a tier chain, so completing this level ends
    // the expedition and no further level is generated. `numeric_seed` is only the
    // reported run identity and drives no generation here.
    explicit Expedition(GameState state, std::uint64_t numeric_seed = 0);

    // The run identity every level of this expedition is derived from.
    [[nodiscard]] std::uint64_t numeric_seed() const noexcept { return numeric_seed_; }

    // The level currently being played.
    [[nodiscard]] GameState& state() noexcept { return state_; }
    [[nodiscard]] const GameState& state() const noexcept { return state_; }

    [[nodiscard]] LevelTier current_tier() const noexcept { return progress_.current_tier(); }
    [[nodiscard]] LevelTier final_tier() const noexcept { return progress_.final_tier(); }
    [[nodiscard]] std::uint32_t completed_levels() const noexcept {
        return progress_.completed_levels();
    }
    [[nodiscard]] std::uint32_t total_levels() const noexcept {
        return static_cast<std::uint32_t>(index_of(progress_.final_tier())) + 1u;
    }
    [[nodiscard]] bool completed() const noexcept { return progress_.completed(); }

    // The running totals carried between levels.
    [[nodiscard]] std::uint64_t total_score() const noexcept { return total_score_; }
    [[nodiscard]] std::uint32_t total_discoveries_found() const noexcept {
        return total_discoveries_found_;
    }
    [[nodiscard]] std::uint32_t total_discoveries_available() const noexcept {
        return total_discoveries_available_;
    }

    // The bonus in force for the level currently being played.
    [[nodiscard]] ExpeditionBonus active_bonus() const noexcept { return active_bonus_; }

    // Every finished level in completion order.
    [[nodiscard]] const std::vector<LevelSummary>& summaries() const noexcept {
        return summaries_;
    }

    // Score the level the actor just finished, fold it into the carried totals,
    // decide the bonus the next level starts with, and advance the chain. On
    // LevelTransition::advanced the next tier's level has already replaced
    // `state()`, so the caller only has to re-present it. Calling this before the
    // current level's objective is complete, or after the expedition has finished,
    // is an explicit no-op that returns LevelTransition::none.
    LevelTransition complete_level(const LevelPerformance& performance);

private:
    std::uint64_t numeric_seed_;
    ExpeditionProgress progress_;
    GameState state_;
    std::uint64_t total_score_ = 0;
    std::uint32_t total_discoveries_found_ = 0;
    std::uint32_t total_discoveries_available_ = 0;
    ExpeditionBonus active_bonus_ = ExpeditionBonus::none;
    std::vector<LevelSummary> summaries_;
};

// Build the GameState for one tier of a run. Exposed so a frontend can preview a
// tier, and so the expedition and its tests share one construction path.
[[nodiscard]] GameState make_level_state(LevelTier tier, std::uint64_t numeric_seed);
