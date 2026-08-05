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

// How much a slot's content is meant to cost the player, expressed as the share
// of the direct spawn-to-exit walk that visiting it *adds* to the journey.
//
// Detour cost is the only thing that is actually expensive here. A generated
// level is open ground: every terrain except deep water and cliff can be walked,
// and the grown blobs never enclose anything, so the shortest walk from spawn to
// exit is the straight-line distance and no arrangement of terrain can lengthen
// it. What a level can charge for is distance off that line. Authoring content by
// detour therefore states the real price directly, instead of inferring it from a
// route the player has no reason to follow.
enum class DetourBand {
    // Nearly free. The player passes this on the way and pays a move or two.
    passing,
    // A real judgement call: worth a visible pause, never a commitment.
    moderate,
    // The level's deliberate side trip, priced to be felt.
    committed,
};

inline constexpr std::uint32_t detour_band_count = 3u;

// A stable, non-localized identifier.
[[nodiscard]] constexpr std::string_view to_string(DetourBand band) noexcept {
    switch (band) {
        case DetourBand::passing:   return "passing";
        case DetourBand::moderate:  return "moderate";
        case DetourBand::committed: return "committed";
    }
    return "passing";
}

// The band's share of the direct walk, in parts per thousand.
[[nodiscard]] constexpr std::uint32_t detour_permille_of(DetourBand band) noexcept {
    switch (band) {
        case DetourBand::passing:   return 100u;
        case DetourBand::moderate:  return 400u;
        case DetourBand::committed: return 700u;
    }
    return 100u;
}

// How widely a single level spreads its content. The profile is seeded per level
// and scales every band at once, so one seed yields a compact level whose content
// clusters near the direct walk and another yields a sprawling one that asks the
// player to range. This is the level-to-level variation in side routes: the
// ordering of the bands never changes, only how much each of them costs.
enum class DetourProfile {
    tight,
    even,
    sprawling,
};

inline constexpr std::uint32_t detour_profile_count = 3u;

// A stable, non-localized identifier.
[[nodiscard]] constexpr std::string_view to_string(DetourProfile profile) noexcept {
    switch (profile) {
        case DetourProfile::tight:     return "tight";
        case DetourProfile::even:      return "even";
        case DetourProfile::sprawling: return "sprawling";
    }
    return "even";
}

// The profile's scaling of every band, in parts per thousand.
[[nodiscard]] constexpr std::uint32_t detour_scale_permille_of(DetourProfile profile) noexcept {
    switch (profile) {
        case DetourProfile::tight:     return 600u;
        case DetourProfile::even:      return 1000u;
        case DetourProfile::sprawling: return 1400u;
    }
    return 1000u;
}

// The extra moves a slot in `band` should cost on a level whose direct walk is
// `direct_distance` moves long. Rounded to nearest, and computed entirely in
// integers so the target is identical on every platform.
[[nodiscard]] constexpr std::uint64_t target_detour_moves(DetourBand band, DetourProfile profile,
                                                          std::uint64_t direct_distance) noexcept {
    const std::uint64_t scaled = direct_distance * detour_permille_of(band) *
                                 detour_scale_permille_of(profile);
    return (scaled + 500000u) / 1000000u;
}

// One reserved place for authored content. `zone` bounds the eligible cells and
// `band` states what visiting it should cost. Generation picks the cell inside
// the zone whose real detour comes closest to the band's target, so the zone
// controls *where* on the map the content sits and the band controls *how far
// out of the player's way* it is.
struct ContentSlot {
    ZoneRect zone{};
    LevelFeatureKind kind = LevelFeatureKind::discovery;
    DetourBand band = DetourBand::passing;
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
// The circuit shapes the level: it is the strip kept clear of terrain, which is
// what makes every level solvable by construction. It is *not* what content is
// priced against. Content sits in three zones chosen for where they lie relative
// to the direct spawn-to-exit walk -- the quadrant the walk crosses, the flank
// beside it, and the half of the map it never enters -- and each zone carries the
// detour band that says what visiting it should cost.
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
