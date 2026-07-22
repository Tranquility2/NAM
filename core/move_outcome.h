#pragma once

#include <cstdint>

#include "coordinates.h"
#include "terrain.h"

// The rule-level result of attempting to move the actor. Presentation text is
// deliberately absent: a frontend maps these values onto messages, animation,
// sound, or particles without parsing strings.
enum class MoveResult {
    moved,               // The actor moved to a new, walkable cell.
    blocked_by_boundary, // The destination was outside the map bounds.
    blocked_by_terrain,  // The destination was in bounds but not walkable.
    blocked_by_stamina,  // The destination was walkable but unaffordable.
};

// A structured description of a move attempt.
//
// - from:    the actor position before the attempt.
// - to:      the actor position after the attempt. On `moved` this is the new
//            cell; on any blocked result the actor does not move, so `to`
//            equals `from`.
// - terrain: the terrain relevant to the outcome. On `moved`,
//            `blocked_by_terrain`, and `blocked_by_stamina` this is the terrain
//            of the attempted destination (the entered cell, the wall that
//            blocked it, or the unaffordable cell); on `blocked_by_boundary`
//            there is no valid destination cell, so it is the terrain the actor
//            is standing on.
// - stamina_cost: the stamina required to enter the outcome's terrain. On
//            `moved` and `blocked_by_stamina` this is the destination terrain's
//            cost; on `blocked_by_boundary` and `blocked_by_terrain` it is 0
//            because no walkable destination cost applies.
// - stamina_before: the actor's current stamina before the attempt.
// - stamina_after:  the actor's current stamina after the attempt. On `moved`
//            this is exactly `stamina_before - stamina_cost`; on every blocked
//            result it equals `stamina_before` because no stamina is spent.
struct MoveOutcome {
    MoveResult result{};
    Coordinates from{};
    Coordinates to{};
    Terrain terrain{};
    std::uint32_t stamina_cost{};
    std::uint32_t stamina_before{};
    std::uint32_t stamina_after{};
};
