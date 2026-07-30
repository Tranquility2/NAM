#include "level_template.h"

#include "direction.h"

namespace {

// Append the cells from `from` to `to` along their shared row or column,
// excluding `from` so callers can chain segments without duplicating corners.
void append_segment(std::vector<Coordinates>& cells, Coordinates from, Coordinates to) {
    const int step_x = (to.x > from.x) ? 1 : (to.x < from.x) ? -1 : 0;
    const int step_y = (to.y > from.y) ? 1 : (to.y < from.y) ? -1 : 0;
    Coordinates current = from;
    while (current != to) {
        current.x += step_x;
        current.y += step_y;
        cells.push_back(current);
    }
}

}  // namespace

LevelTemplate template_of(LevelTier tier) {
    const LevelDimensions dimensions = dimensions_of(tier);
    const Coordinates spawn = center_spawn_of(tier);
    const int width = static_cast<int>(dimensions.width);
    const int height = static_cast<int>(dimensions.height);

    // Interior bounds. The outermost ring is always solid wall.
    const int max_x = width - 2;
    const int max_y = height - 2;

    // The three fixed corridors: a low return run, the left flank, and the top
    // crossing. Each sits one cell inside the wall ring so the route never hugs
    // the boundary, which keeps a walkable rim available for side content.
    const int low_y = max_y - 1;
    const int left_x = 3;
    const int top_y = 2;

    LevelTemplate level;
    level.entry_zone = ZoneRect{spawn.x - 1, spawn.y - 1, spawn.x + 1, spawn.y + 1};
    level.exit_zone = ZoneRect{max_x - 3, 1, max_x, top_y + 1};
    level.route_waypoints = {
        Coordinates{spawn.x, low_y},
        Coordinates{left_x, low_y},
        Coordinates{left_x, top_y},
    };
    level.branch_spurs = {
        BranchSpur{Coordinates{spawn.x - 3, low_y}, Direction::down, 1},
        BranchSpur{Coordinates{left_x, spawn.y}, Direction::right, 3},
        BranchSpur{Coordinates{spawn.x - 3, top_y}, Direction::up, 1},
    };
    // The hazard sits on the low corridor and the safe landmark on the left flank,
    // so both are on the main route: the player meets the hazard decision early and
    // the recovery waypoint afterwards. The discovery sits in the quadrant the
    // route never crosses, so finding it always costs a deliberate detour.
    level.content_slots = {
        ContentSlot{ZoneRect{spawn.x - 6, low_y, spawn.x - 4, low_y}, LevelFeatureKind::hazard,
                    true},
        ContentSlot{ZoneRect{left_x, spawn.y - 1, left_x, spawn.y + 1},
                    LevelFeatureKind::safe_landmark, true},
        ContentSlot{ZoneRect{spawn.x + 2, spawn.y + 2, max_x - 1, max_y},
                    LevelFeatureKind::discovery, false},
    };
    return level;
}

std::vector<Coordinates> route_cells(const LevelTemplate& level, Coordinates spawn,
                                     Coordinates exit_cell) {
    std::vector<Coordinates> cells;
    cells.push_back(spawn);

    Coordinates current = spawn;
    for (const Coordinates waypoint : level.route_waypoints) {
        append_segment(cells, current, waypoint);
        current = waypoint;
    }

    // The final approach crosses to the exit column first, then drops or climbs to
    // the exit row, so the route always enters the exit zone along the top
    // corridor rather than cutting diagonally out of the authored shape.
    const Coordinates corner{exit_cell.x, current.y};
    append_segment(cells, current, corner);
    append_segment(cells, corner, exit_cell);
    return cells;
}

std::vector<Coordinates> spur_cells(const BranchSpur& spur) {
    const Coordinates step = direction_delta(spur.direction);
    std::vector<Coordinates> cells;
    Coordinates current = spur.anchor;
    for (int index = 0; index < spur.length; ++index) {
        current.x += step.x;
        current.y += step.y;
        cells.push_back(current);
    }
    return cells;
}
