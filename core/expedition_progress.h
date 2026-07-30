#pragma once

#include <cstdint>
#include <optional>

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
// Gameplay state composes this value; keeping the transition here prevents
// frontends from re-deriving it.
//
// `final_tier` is the last tier the expedition plays. It exists so the prototype
// can ship a shorter Small -> Medium expedition while the tier table already
// describes all four scales; the full expedition is simply the default.
class ExpeditionProgress {
public:
    ExpeditionProgress() = default;
    explicit ExpeditionProgress(LevelTier final_tier) noexcept : final_tier_(final_tier) {}

    [[nodiscard]] LevelTier current_tier() const noexcept { return current_tier_; }
    [[nodiscard]] LevelTier final_tier() const noexcept { return final_tier_; }
    [[nodiscard]] std::uint32_t completed_levels() const noexcept { return completed_levels_; }
    [[nodiscard]] bool completed() const noexcept { return completed_; }

    [[nodiscard]] LevelTransition complete_current_level() noexcept {
        if (completed_) {
            return LevelTransition::none;
        }

        ++completed_levels_;
        const std::optional<LevelTier> next =
            current_tier_ == final_tier_ ? std::nullopt : next_level_tier(current_tier_);
        if (next.has_value()) {
            current_tier_ = *next;
            return LevelTransition::advanced;
        }

        completed_ = true;
        return LevelTransition::expedition_completed;
    }

private:
    LevelTier current_tier_ = LevelTier::small;
    LevelTier final_tier_ = LevelTier::x_large;
    std::uint32_t completed_levels_ = 0;
    bool completed_ = false;
};
