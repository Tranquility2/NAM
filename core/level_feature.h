#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "coordinates.h"

// Authored level content that is semantic state rather than terrain. A level
// template reserves slots for content; a seed places the exact cells; the core
// applies their rules. Nothing here carries display text, so a frontend chooses
// its own glyphs and wording.
//
// The prototype ships one family of each kind:
//
// - discovery:     an optional find, off the main route. Entering it the first
//                  time records a discovery. It changes no stamina, so the
//                  reward for detouring is score and story, never survival.
// - hazard:        a visible cell that is expensive to cross. Entering it charges
//                  an extra stamina penalty on top of the terrain cost. It never
//                  blocks the step, so a hazard is a route-quality decision and
//                  never a barrier.
// - safe_landmark: a resting waypoint on a plausible forward route. Entering it
//                  restores the stamina meter completely, every time, with no
//                  command and no state to track.
enum class LevelFeatureKind {
    discovery,
    hazard,
    safe_landmark,
};

// A stable, non-localized identifier. Frontends map it to their own wording.
[[nodiscard]] constexpr std::string_view to_string(LevelFeatureKind kind) noexcept {
    switch (kind) {
        case LevelFeatureKind::discovery:     return "discovery";
        case LevelFeatureKind::hazard:        return "hazard";
        case LevelFeatureKind::safe_landmark: return "safe_landmark";
    }
    return "discovery";
}

// The extra stamina a hazard charges on entry, on top of the destination
// terrain's own cost. The charge saturates at zero like every other cost, so a
// hazard can empty the meter but can never refuse the step.
inline constexpr std::uint32_t hazard_stamina_penalty = 6u;

// One placed piece of authored content.
struct LevelFeature {
    Coordinates position{};
    LevelFeatureKind kind = LevelFeatureKind::discovery;
};

// The authored overlay a level template placed on a map: an exit the template
// chose and the content it seeded. Handcrafted and parsed maps carry an empty
// layout, so the objective derives an exit and no content applies.
struct LevelLayout {
    std::optional<Coordinates> exit;
    std::vector<LevelFeature> features;
};
