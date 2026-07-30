#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "coordinates.h"
#include "direction.h"
#include "level_feature.h"
#include "level_tier.h"

// The authored shape of a level. A tier template fixes *where play happens*: the
// entry zone, the exit zone, the readable main route between them, and the points
// where an optional side route may attach. A seed then fixes *what is found*
// inside that shape: the exact exit position within the exit zone, which side
// routes open, and every terrain feature grown around the route.
//
// Templates are pure data derived from the tier's dimensions and centre spawn, so
// they contain no randomness, no I/O, and no presentation concern. Generation
// carves the route described here before growing terrain, which is what makes a
// generated level solvable by construction rather than by rejection sampling.

// Which authored corner of the map holds the exit. The set of corners is fixed by
// the template; which one a level uses is seeded, so the broad direction revealed
// at the landmark actually carries information instead of naming the same corner
// every run.
enum class ExitCorner {
    top_right,
    top_left,
    bottom_right,
    bottom_left,
};

inline constexpr std::uint32_t exit_corner_count = 4u;

// True when the corner lies on the right half of the map.
[[nodiscard]] constexpr bool corner_is_right(ExitCorner corner) noexcept {
    return corner == ExitCorner::top_right || corner == ExitCorner::bottom_right;
}

// True when the corner lies on the top half of the map.
[[nodiscard]] constexpr bool corner_is_top(ExitCorner corner) noexcept {
    return corner == ExitCorner::top_right || corner == ExitCorner::top_left;
}

// A stable, non-localized identifier.
[[nodiscard]] constexpr std::string_view to_string(ExitCorner corner) noexcept {
    switch (corner) {
        case ExitCorner::top_right:    return "top_right";
        case ExitCorner::top_left:     return "top_left";
        case ExitCorner::bottom_right: return "bottom_right";
        case ExitCorner::bottom_left:  return "bottom_left";
    }
    return "top_right";
}

// A closed, inclusive rectangle of interior cells.
struct ZoneRect {
    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;

    [[nodiscard]] constexpr bool contains(Coordinates position) const noexcept {
        return position.x >= min_x && position.x <= max_x && position.y >= min_y &&
               position.y <= max_y;
    }

    [[nodiscard]] constexpr std::size_t width() const noexcept {
        return static_cast<std::size_t>(max_x - min_x + 1);
    }

    [[nodiscard]] constexpr std::size_t height() const noexcept {
        return static_cast<std::size_t>(max_y - min_y + 1);
    }
};

// One authored attachment point for an optional side route. The spur leaves the
// main route at `anchor` in `direction` for `length` cells. Whether it opens at
// all is a seeded decision; the template only says where a spur is allowed.
struct BranchSpur {
    Coordinates anchor{};
    Direction direction = Direction::up;
    int length = 0;
};

// One reserved place for authored content. `zone` bounds the eligible cells and
// `on_route` records whether the slot deliberately sits on the main route, which
// is what distinguishes a hazard the player must decide about from a discovery
// that rewards leaving the route.
struct ContentSlot {
    ZoneRect zone{};
    LevelFeatureKind kind = LevelFeatureKind::discovery;
    bool on_route = false;
};

// Everything a tier fixes about a level's shape.
struct LevelTemplate {
    // The protected square around the spawn. No terrain feature is grown here, so
    // the player always starts with room to read the map.
    ZoneRect entry_zone{};
    // The region that always holds the exit. The exact cell inside it is seeded.
    ZoneRect exit_zone{};
    // Interior corners of the main route, in order, starting after the spawn and
    // ending at the last corner before the exit. Consecutive points always share a
    // row or a column, so the route is a sequence of orthogonal segments.
    std::vector<Coordinates> route_waypoints;
    // Where optional side routes may attach to the main route.
    std::vector<BranchSpur> branch_spurs;
    // The content budget: one slot per placed feature. A slot names a kind and the
    // region it may occupy; the seed picks the exact walkable cell inside it. The
    // budget is therefore fixed by the tier while the placement varies per seed.
    std::vector<ContentSlot> content_slots;
};

// The authored template for a tier and a seeded exit corner. One shape formula
// serves all four tiers and all four corners: the route leaves the centre spawn
// toward the vertical extreme *opposite* the exit, runs along that corridor to the
// horizontal flank *opposite* the exit, climbs or drops that flank, then crosses
// back toward the exit corner. That produces a long, readable circuit whose two
// ends are far apart, so the direct line between them is never the route.
//
// Content follows the same mirroring: the hazard sits on the first corridor and
// the safe landmark on the flank, while the discovery sits in the quadrant the
// route never crosses, which is the exit's horizontal side and the opposite
// vertical side.
[[nodiscard]] LevelTemplate template_of(LevelTier tier, ExitCorner corner);

// The ordered cells of the main route for one seeded exit: the spawn, every
// waypoint segment, and the final legs into `exit_cell`. Adjacent duplicates are
// removed, so the result is a simple cardinal-connected path from the tier's
// spawn to `exit_cell`.
//
// Precondition: `exit_cell` lies inside `level.exit_zone`.
[[nodiscard]] std::vector<Coordinates> route_cells(const LevelTemplate& level, Coordinates spawn,
                                                   Coordinates exit_cell);

// The ordered cells of one spur, excluding its anchor (which already belongs to
// the main route).
[[nodiscard]] std::vector<Coordinates> spur_cells(const BranchSpur& spur);
