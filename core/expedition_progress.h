#pragma once

#include <cstdint>

#include "level_tier.h"

// The typed result of completing the current level. `advanced` means the next
// tier became current; `expedition_completed` means X-Large was completed; `none`
// makes repeated completion calls explicit no-ops.
enum class LevelTransition {
    none,
    advanced,
    expedition_completed,
};

// Core-owned progress through one Small -> Medium -> Large -> X-Large expedition.
// Gameplay state will compose this value as the V2 objective and level-generation
// work lands; keeping the transition here prevents frontends from re-deriving it.
class ExpeditionProgress {
public:
    [[nodiscard]] LevelTier current_tier() const noexcept { return current_tier_; }
    [[nodiscard]] std::uint32_t completed_levels() const noexcept { return completed_levels_; }
    [[nodiscard]] bool completed() const noexcept { return completed_; }

    [[nodiscard]] LevelTransition complete_current_level() noexcept {
        if (completed_) {
            return LevelTransition::none;
        }

        ++completed_levels_;
        const std::optional<LevelTier> next = next_level_tier(current_tier_);
        if (next.has_value()) {
            current_tier_ = *next;
            return LevelTransition::advanced;
        }

        completed_ = true;
        return LevelTransition::expedition_completed;
    }

private:
    LevelTier current_tier_ = LevelTier::small;
    std::uint32_t completed_levels_ = 0;
    bool completed_ = false;
};
