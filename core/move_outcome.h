#pragma once

#include <cstdint>
#include <optional>

#include "coordinates.h"
#include "level_feature.h"
#include "terrain.h"

// The rule-level result of attempting to move the actor. Presentation text is
// deliberately absent: a frontend maps these values onto messages, animation,
// sound, or particles without parsing strings.
enum class MoveResult {
    moved,                // The actor moved to a new, walkable cell.
    blocked_by_boundary,  // The destination was outside the map bounds.
    blocked_by_terrain,   // The destination was in bounds but not walkable.
};

// A structured description of a move attempt.
//
// Movement is fluid: a walkable in-bounds destination always succeeds. No meter
// can refuse a step, so the only two failures are the map's edge and impassable
// terrain.
//
// - from:    the actor position before the attempt.
// - to:      the actor position after the attempt. On `moved` this is the new
//            cell; on any blocked result the actor does not move, so `to`
//            equals `from`.
// - terrain: the terrain relevant to the outcome. On `moved` and
//            `blocked_by_terrain` this is the terrain of the attempted
//            destination (the entered cell or the barrier that blocked it); on
//            `blocked_by_boundary` there is no valid destination cell, so it is
//            the terrain the actor is standing on.
// - feature: the authored content on the outcome's destination cell, if any. It
//            is empty on every blocked result and on any cell the level template
//            left plain, so a frontend never re-derives content from coordinates.
struct MoveOutcome {
    MoveResult result{};
    Coordinates from{};
    Coordinates to{};
    Terrain terrain{};
    std::optional<LevelFeatureKind> feature{};
};
