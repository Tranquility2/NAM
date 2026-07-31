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
// Movement is fluid: a walkable in-bounds destination always succeeds. Stamina
// can never refuse a step, so the only two failures are the map's edge and
// impassable terrain.
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
// - stamina_cost: the stamina charged for entering the outcome's terrain, plus
//            hazard_stamina_penalty when the destination carries a hazard. On
//            every blocked result it is 0 because no walkable destination cost
//            applies. The charge saturates at zero, so it is an upper bound on the
//            stamina actually lost rather than a guaranteed deduction. A hazard
//            therefore makes a step expensive but never illegal.
// - stamina_recovered: the stamina automatically regained by the step, after the
//            cost was charged. On `moved` this is the destination terrain's
//            passive_recovery_of amount clamped by the maximum, or the full
//            restoration granted by reaching the level landmark for the first
//            time or by entering a safe landmark; it is 0 on every blocked
//            result.
// - stamina_before: the actor's current stamina before the attempt.
// - stamina_after:  the actor's current stamina after the attempt. On `moved`
//            this is the saturating charge of `stamina_cost` followed by
//            `stamina_recovered`; on every blocked result it equals
//            `stamina_before` because no stamina changes.
struct MoveOutcome {
    MoveResult result{};
    Coordinates from{};
    Coordinates to{};
    Terrain terrain{};
    std::uint32_t stamina_cost{};
    std::uint32_t stamina_recovered{};
    std::uint32_t stamina_before{};
    std::uint32_t stamina_after{};
    // Declared last and default-initialized so existing positional construction of
    // a MoveOutcome stays valid.
    std::optional<LevelFeatureKind> feature{};
};
