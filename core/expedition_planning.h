#pragma once

#include <cstdint>

#include "coordinates.h"
#include "map.h"

// Frontend-neutral, deterministic expedition planning. A finite-state search over
// (position, stamina, daylight hours used, objective phase) computes the minimum
// feasible completion time and provisions for a beacon round trip, using move,
// emergency-rest, normal-camp, and bivouac transitions identical to the live
// GameState rules (REQ-015 / REQ-016 / RISK-002). It never touches a terminal,
// prose, clock, environment, or mutable global state.

// The map-derived planning baseline for a beacon round trip.
//   * minimum_completion_days is one more than the fewest overnight transitions
//     (normal camps plus bivouacs) any feasible plan uses.
//   * minimum_required_provisions is the smallest number of provisions consumed
//     among every plan that completes in exactly minimum_completion_days.
// Both are at least well-defined for a single-cell objective (days 1, provisions
// 0). The pair is minimized lexicographically by (overnights, provisions).
struct ExpeditionPlanBaseline {
    std::uint32_t minimum_completion_days = 1;
    std::uint64_t minimum_required_provisions = 0;
};

// Compute the planning baseline for the round trip from `spawn` out to `beacon`
// and back over the map, using `max_stamina` as the stamina cap. A move spends
// the destination terrain's travel hours and stamina; an emergency rest spends
// emergency_rest_hours of daylight and one provision to recover the current
// terrain's rest amount capped at max_stamina; a normal camp (open, fields, hill)
// spends one provision, sets stamina to max_stamina, and starts a new day; a
// bivouac (water, mountain) spends two provisions, sets stamina to 10, and starts
// a new day. Camps require at least one elapsed daylight hour or below-cap
// stamina. The search minimizes (overnight transitions, provisions) and returns
// minimum_completion_days = overnights + 1 with the matching minimum provisions.
// A beacon at spawn returns days 1 and provisions 0.
[[nodiscard]] ExpeditionPlanBaseline compute_expedition_plan_baseline(const Map& map,
                                                                     Coordinates spawn,
                                                                     Coordinates beacon,
                                                                     std::uint32_t max_stamina);
