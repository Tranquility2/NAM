#pragma once

#include <cstdint>

#include "coordinates.h"
#include "expedition_time.h"
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
// Movement is fluid: a walkable in-bounds destination always succeeds. Neither
// stamina nor remaining daylight can refuse a step, so the only two failures are
// the map's edge and impassable terrain.
//
// - from:    the actor position before the attempt.
// - to:      the actor position after the attempt. On `moved` this is the new
//            cell; on any blocked result the actor does not move, so `to`
//            equals `from`.
// - terrain: the terrain relevant to the outcome. On `moved` and
//            `blocked_by_terrain` this is the terrain of the attempted
//            destination (the entered cell or the wall that blocked it); on
//            `blocked_by_boundary` there is no valid destination cell, so it is
//            the terrain the actor is standing on.
// - stamina_cost: the stamina charged for entering the outcome's terrain. On
//            `moved` this is the destination terrain's cost; on every blocked
//            result it is 0 because no walkable destination cost applies. The
//            charge saturates at zero, so it is an upper bound on the stamina
//            actually lost rather than a guaranteed deduction.
// - stamina_recovered: the stamina automatically regained by the step, after the
//            cost was charged. On `moved` this is the destination terrain's
//            passive_recovery_of amount clamped by the maximum, or the full
//            restoration granted by reaching the level landmark for the first
//            time; it is 0 on every blocked result.
// - stamina_before: the actor's current stamina before the attempt.
// - stamina_after:  the actor's current stamina after the attempt. On `moved`
//            this is the saturating charge of `stamina_cost` followed by
//            `stamina_recovered`; on every blocked result it equals
//            `stamina_before` because no stamina changes.
// - travel_hours: the daylight hours entering the outcome's terrain requires. Set
//            on `moved`; 0 on boundary and terrain blocks.
// - time_before/time_after: the expedition time bracketing the attempt. On
//            `moved`, after raises daylight_hours_used by travel_hours; on every
//            blocked result they are equal because no daylight is spent.
struct MoveOutcome {
    MoveResult result{};
    Coordinates from{};
    Coordinates to{};
    Terrain terrain{};
    std::uint32_t stamina_cost{};
    std::uint32_t stamina_recovered{};
    std::uint32_t stamina_before{};
    std::uint32_t stamina_after{};
    std::uint32_t travel_hours{};
    ExpeditionTime time_before{};
    ExpeditionTime time_after{};
};
