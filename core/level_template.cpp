#include "level_template.h"

#include <algorithm>
#include <array>

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

LevelTemplate template_of(LevelTier tier, ExitCorner corner) {
    const LevelDimensions dimensions = dimensions_of(tier);
    const Coordinates spawn = center_spawn_of(tier);
    const int width = static_cast<int>(dimensions.width);
    const int height = static_cast<int>(dimensions.height);

    // Interior bounds. The outermost ring is always solid cliff.
    const int max_x = width - 2;
    const int max_y = height - 2;

    // The four candidate corridors: a low run, a top run, and the two flanks. Each
    // sits one cell inside the cliff ring so the route never hugs the boundary,
    // which keeps a walkable rim available for side content.
    const int low_y = max_y - 1;
    const int top_y = 2;
    const int left_x = 3;
    const int right_x = max_x - 2;

    const bool exit_right = corner_is_right(corner);
    const bool exit_top = corner_is_top(corner);

    // The route always sets out for the corridor and flank opposite the exit, so
    // the circuit stays long no matter which corner the seed picked.
    const int first_y = exit_top ? low_y : top_y;
    const int approach_y = exit_top ? top_y : low_y;
    const int flank_x = exit_right ? left_x : right_x;

    const int spur_x = exit_right ? (spawn.x - 3) : (spawn.x + 3);
    const Direction first_spur = exit_top ? Direction::down : Direction::up;
    const Direction flank_spur = exit_right ? Direction::right : Direction::left;
    const Direction approach_spur = exit_top ? Direction::up : Direction::down;

    LevelTemplate level;
    level.entry_zone = ZoneRect{spawn.x - 1, spawn.y - 1, spawn.x + 1, spawn.y + 1};
    const int exit_min_x = exit_right ? (max_x - 3) : 1;
    const int exit_max_x = exit_right ? max_x : 4;
    const int exit_min_y = exit_top ? 1 : (max_y - 2);
    const int exit_max_y = exit_top ? (top_y + 1) : max_y;
    level.exit_zone = ZoneRect{exit_min_x, exit_min_y, exit_max_x, exit_max_y};
    level.route_waypoints = {
        Coordinates{spawn.x, first_y},
        Coordinates{flank_x, first_y},
        Coordinates{flank_x, approach_y},
    };
    level.branch_spurs = {
        BranchSpur{Coordinates{spur_x, first_y}, first_spur, 1},
        BranchSpur{Coordinates{flank_x, spawn.y}, flank_spur, 3},
        BranchSpur{Coordinates{spur_x, approach_y}, approach_spur, 1},
    };
    // Content is priced by how far off the direct spawn-to-exit walk it sits, and
    // each zone is placed so that its band's target is actually reachable inside
    // it. The first vantage point sits in the quadrant the direct walk crosses,
    // near the spawn, so the player is shown what a wide reveal does before it has
    // cost anything. The second sits on the flank beside that walk, where stepping
    // aside is a visible but bounded price. The discovery sits in the half of the
    // map the direct walk never enters, so finding it is always a deliberate trip.
    //
    // Every zone spans a wide range of detours rather than a handful of cells,
    // which is what lets the seeded profile move the content in or out. A zone
    // pinned to three cells would fix its price no matter what the profile said.
    const int mid_x = exit_right ? ((spawn.x + max_x) / 2) : ((spawn.x + 1) / 2);
    const int mid_y = exit_top ? ((spawn.y + 1) / 2) : ((spawn.y + max_y) / 2);
    const ZoneRect near_zone{std::min(spawn.x - 3, mid_x), std::min(spawn.y - 3, mid_y),
                             std::max(spawn.x + 3, mid_x), std::max(spawn.y + 3, mid_y)};
    const ZoneRect flank_zone{exit_right ? 1 : spawn.x, exit_top ? 1 : spawn.y,
                              exit_right ? spawn.x : max_x, exit_top ? spawn.y : max_y};
    const ZoneRect far_zone{1, exit_top ? spawn.y : 1, max_x, exit_top ? max_y : spawn.y};
    level.content_slots = {
        ContentSlot{near_zone, LevelFeatureKind::vantage_point, DetourBand::passing},
        ContentSlot{flank_zone, LevelFeatureKind::vantage_point, DetourBand::moderate},
        ContentSlot{far_zone, LevelFeatureKind::discovery, DetourBand::committed},
    };

    // Content scales with the tier so the biggest level is not the emptiest. A
    // Small level has 164 reachable cells and an X-Large one has 1209, so the
    // three slots above -- which were the whole of a level's content on every
    // tier -- left X-Large offering one discovery per 1209 cells against Small's
    // one per 164.
    //
    // Every tier above Small adds one discovery and one vantage point, giving
    // 1/2/3/4 discoveries and 2/3/4/5 vantage points. The extra slots reuse the
    // three zones with different bands rather than inventing new regions: the
    // band is what prices a detour, so the same zone at a different band is a
    // genuinely different offer, and `place_content` already refuses to put two
    // features on one cell.
    const std::array<ContentSlot, 6> extra{{
        ContentSlot{flank_zone, LevelFeatureKind::discovery, DetourBand::moderate},
        ContentSlot{far_zone, LevelFeatureKind::vantage_point, DetourBand::committed},
        ContentSlot{near_zone, LevelFeatureKind::discovery, DetourBand::passing},
        ContentSlot{near_zone, LevelFeatureKind::vantage_point, DetourBand::moderate},
        ContentSlot{far_zone, LevelFeatureKind::discovery, DetourBand::passing},
        ContentSlot{flank_zone, LevelFeatureKind::vantage_point, DetourBand::passing},
    }};
    const std::size_t extra_pairs = extra_content_pairs_of(tier);
    for (std::size_t pair = 0; pair < extra_pairs; ++pair) {
        level.content_slots.push_back(extra[pair * 2u]);
        level.content_slots.push_back(extra[pair * 2u + 1u]);
    }
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
    // the exit row, so the route always enters the exit zone along the approach
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
