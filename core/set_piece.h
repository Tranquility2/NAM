#pragma once

#include <cstddef>
#include <string_view>

#include "coordinates.h"
#include "level_tier.h"
#include "terrain.h"

// The one terrain set-piece a tier lays across the player's way.
//
// Every other piece of authored content is optional: a discovery or a vantage
// point is somewhere to *choose* to go, priced in the detour it costs (see
// level_template.h). A set-piece is the opposite. It is a band of terrain that
// spans the level from edge to edge between spawn and exit, so any route to the
// exit has to cross it, and crossing costs nothing. It is what the level makes
// the player meet rather than what it invites them to find.
//
// Set-pieces never block. Each one is built from walkable terrain, and because
// the band replaces whatever it covers, it can only add ways through a level,
// never remove them. What it changes is what the crossing looks like and how far
// the player can see while making it, which is the whole point: terrain governs
// sight, so wading a lake and topping a pass are different moments.
//
// One kind per tier, so the set-piece is part of what a tier *is*.

enum class SetPieceKind {
    // A single line of shallow water: a stream with a crossing. Small.
    ford,
    // A double line of hills. Higher ground, and a wider view from on top.
    ridge,
    // A broad shallow lake the player wades rather than steps over, with the
    // shortest sight in the game while crossing it.
    lakeshore,
    // A band of mountain: the widest view anywhere, and the longest climb.
    high_pass,
};

inline constexpr std::size_t set_piece_kind_count = 4u;

// A stable, non-localized identifier. Frontends choose their own wording.
[[nodiscard]] constexpr std::string_view to_string(SetPieceKind kind) noexcept {
    switch (kind) {
        case SetPieceKind::ford:      return "ford";
        case SetPieceKind::ridge:     return "ridge";
        case SetPieceKind::lakeshore: return "lakeshore";
        case SetPieceKind::high_pass: return "high_pass";
    }
    return "ford";
}

// The set-piece a tier lays down. Fixed rather than seeded: a tier is recognised
// partly by the crossing it always has.
[[nodiscard]] constexpr SetPieceKind set_piece_of(LevelTier tier) noexcept {
    switch (tier) {
        case LevelTier::small:   return SetPieceKind::ford;
        case LevelTier::medium:  return SetPieceKind::ridge;
        case LevelTier::large:   return SetPieceKind::lakeshore;
        case LevelTier::x_large: return SetPieceKind::high_pass;
    }
    return SetPieceKind::ford;
}

// The terrain the band is made of. Every value is walkable, which is what keeps
// a set-piece a crossing rather than a wall.
[[nodiscard]] constexpr Terrain set_piece_terrain_of(SetPieceKind kind) noexcept {
    switch (kind) {
        case SetPieceKind::ford:      return Terrain::shallow_water;
        case SetPieceKind::ridge:     return Terrain::hill;
        case SetPieceKind::lakeshore: return Terrain::shallow_water;
        case SetPieceKind::high_pass: return Terrain::mountain;
    }
    return Terrain::shallow_water;
}

// How many cells deep the band is. Depth is what separates the stream from the
// lake: they are the same water, but one is a step and the other is a wade.
[[nodiscard]] constexpr int set_piece_depth_of(SetPieceKind kind) noexcept {
    switch (kind) {
        case SetPieceKind::ford:      return 1;
        case SetPieceKind::ridge:     return 2;
        case SetPieceKind::lakeshore: return 3;
        case SetPieceKind::high_pass: return 2;
    }
    return 1;
}

// Where a level's set-piece ended up, and what it is.
//
// The band occupies a closed, inclusive range of *columns* placed strictly
// between spawn and the exit. That range is the crossing: a step changes x by at
// most one, so getting from spawn's column to the exit's column means standing in
// one of these columns, whatever route is taken. `is_crossing` asks that
// question, and deliberately ignores y for exactly this reason.
//
// The band's terrain is written to a narrower range of rows than the level has,
// so grown terrain can still flow past the ends of the band. `covers` asks which
// cells actually hold the set-piece's terrain, which is a different and smaller
// question than whether the player is crossing it.
struct SetPieceRegion {
    SetPieceKind kind = SetPieceKind::ford;
    int min_x = 0;
    int max_x = 0;
    // The rows the band's terrain was written to.
    int min_y = 0;
    int max_y = 0;

    // Whether standing here means crossing the set-piece.
    [[nodiscard]] constexpr bool is_crossing(Coordinates position) const noexcept {
        return position.x >= min_x && position.x <= max_x;
    }

    // Whether this cell is one the band wrote its terrain over.
    [[nodiscard]] constexpr bool covers(Coordinates position) const noexcept {
        return is_crossing(position) && position.y >= min_y && position.y <= max_y;
    }
};
