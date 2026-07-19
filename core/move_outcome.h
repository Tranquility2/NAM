#pragma once

#include "coordinates.h"
#include "terrain.h"

// The rule-level result of attempting to move the actor. Presentation text is
// deliberately absent: a frontend maps these values onto messages, animation,
// sound, or particles without parsing strings.
enum class MoveResult {
    moved,               // The actor moved to a new, walkable cell.
    blocked_by_boundary, // The destination was outside the map bounds.
    blocked_by_terrain,  // The destination was in bounds but not walkable.
};

// A structured description of a move attempt.
//
// - from:    the actor position before the attempt.
// - to:      the actor position after the attempt. On `moved` this is the new
//            cell; on any blocked result the actor does not move, so `to`
//            equals `from`.
// - terrain: the terrain relevant to the outcome. On `moved` and
//            `blocked_by_terrain` this is the terrain of the attempted
//            destination (the entered cell, or the wall that blocked it); on
//            `blocked_by_boundary` there is no valid destination cell, so it is
//            the terrain the actor is standing on.
struct MoveOutcome {
    MoveResult result{};
    Coordinates from{};
    Coordinates to{};
    Terrain terrain{};
};
