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
//                  time records a discovery, so the reward for detouring is score
//                  and story.
// - vantage_point: a viewpoint on a plausible forward route. Entering it the
//                  first time grants one wide reveal, larger than any terrain's
//                  own sight radius, and is then inert. It is the reward for
//                  taking the scenic line rather than the direct one.
enum class LevelFeatureKind {
    discovery,
    vantage_point,
};

// A stable, non-localized identifier. Frontends map it to their own wording.
[[nodiscard]] constexpr std::string_view to_string(LevelFeatureKind kind) noexcept {
    switch (kind) {
        case LevelFeatureKind::discovery:     return "discovery";
        case LevelFeatureKind::vantage_point: return "vantage_point";
    }
    return "discovery";
}

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
