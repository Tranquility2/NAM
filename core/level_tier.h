#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include "coordinates.h"

// The four fixed level scales in one V2 expedition. Their dimensions and centre
// spawn points are compatibility data: generation recipes may vary content inside
// a tier, but they do not vary the tier's rectangular bounds.
enum class LevelTier {
    small,
    medium,
    large,
    x_large,
};

struct LevelDimensions {
    std::size_t width = 0;
    std::size_t height = 0;
};

[[nodiscard]] constexpr LevelDimensions dimensions_of(LevelTier tier) noexcept {
    switch (tier) {
        case LevelTier::small:   return LevelDimensions{21u, 11u};
        case LevelTier::medium:  return LevelDimensions{29u, 15u};
        case LevelTier::large:   return LevelDimensions{39u, 21u};
        case LevelTier::x_large: return LevelDimensions{51u, 27u};
    }
    return LevelDimensions{21u, 11u};
}

[[nodiscard]] constexpr Coordinates center_spawn_of(LevelTier tier) noexcept {
    switch (tier) {
        case LevelTier::small:   return Coordinates{10, 5};
        case LevelTier::medium:  return Coordinates{14, 7};
        case LevelTier::large:   return Coordinates{19, 10};
        case LevelTier::x_large: return Coordinates{25, 13};
    }
    return Coordinates{10, 5};
}

[[nodiscard]] constexpr std::size_t index_of(LevelTier tier) noexcept {
    switch (tier) {
        case LevelTier::small:   return 0u;
        case LevelTier::medium:  return 1u;
        case LevelTier::large:   return 2u;
        case LevelTier::x_large: return 3u;
    }
    return 0u;
}

[[nodiscard]] constexpr std::string_view to_string(LevelTier tier) noexcept {
    switch (tier) {
        case LevelTier::small:   return "Small";
        case LevelTier::medium:  return "Medium";
        case LevelTier::large:   return "Large";
        case LevelTier::x_large: return "X-Large";
    }
    return "Small";
}

[[nodiscard]] constexpr std::optional<LevelTier> next_level_tier(LevelTier tier) noexcept {
    switch (tier) {
        case LevelTier::small:   return LevelTier::medium;
        case LevelTier::medium:  return LevelTier::large;
        case LevelTier::large:   return LevelTier::x_large;
        case LevelTier::x_large: return std::nullopt;
    }
    return std::nullopt;
}
