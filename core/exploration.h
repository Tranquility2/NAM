#pragma once

#include <cstdint>

#include "map.h"
#include "visibility.h"

// Frontend-neutral exploration counts derived from a map and an exploration
// memory snapshot. These are the core-owned typed inputs to the rescued
// exploration score, so every frontend agrees on the exploration fraction without
// re-deriving reachability or reading terrain behind fog.

// The number of walkable cells reachable from the map's spawn over cardinal
// walkable steps, including spawn itself. This is the denominator of the rescued
// exploration score and equals BeaconObjective::total_reachable_walkable_cells.
[[nodiscard]] std::uint64_t count_reachable_walkable_cells(const Map& map);

// The number of reachable walkable cells (as above) whose visibility is not
// unexplored, i.e. cells the actor has explored at least once. This is the
// numerator of the rescued exploration score. `visibility` must mirror the map's
// dimensions.
[[nodiscard]] std::uint64_t count_explored_reachable_walkable_cells(
    const Map& map, const VisibilityMap& visibility);
