#pragma once

#include <cstdint>
#include <optional>

// Terrain is the semantic type stored for every map cell. Rendering glyphs,
// passability, and sight range are derived from the enum via the total functions
// below, so the core never stores presentation characters or mutates a lookup
// table.
//
// Terrain has two jobs: whether an actor may stand on it, and how far an actor
// standing on it can see. Choosing where to walk is a question of what you will
// be able to see from there, which is why visibility_radius_of is the most
// interesting mapping in this file.
enum class Terrain : unsigned char {
    open,           // '.'
    mountain,       // '@'
    shallow_water,  // '~'
    fields,         // 'x'
    hill,           // '^'
    forest,         // '&'
    deep_water,     // '='
    cliff,          // '#'
};

// The exploration/sight radius revealed around an actor standing on this
// terrain, as a square of this many cells in each direction. This is the single
// source of truth for terrain-based visibility; GameState and tests consume it
// instead of duplicating the mapping.
//
// The spread is deliberately wide so that terrain choice is legible without a
// tutorial. Forest is the only terrain worse than the baseline: standing among
// the trees an actor sees almost nothing, which is what makes the short route
// through the woods a real decision rather than a free one. Shallow water beats
// open ground because it is a clearing an actor can wade into and see across.
// Hills and mountains are the payoff: a mountain redraws a large piece of the
// level in a single step.
//
// Impassable terrain reports 0. An actor can never stand on a cliff or in deep
// water, so the value only documents that the case is unreachable.
[[nodiscard]] constexpr int visibility_radius_of(Terrain terrain) noexcept {
    switch (terrain) {
        case Terrain::forest:        return 1;
        case Terrain::open:          return 3;
        case Terrain::fields:        return 3;
        case Terrain::shallow_water: return 4;
        case Terrain::hill:          return 5;
        case Terrain::mountain:      return 7;
        case Terrain::deep_water:    return 0;
        case Terrain::cliff:         return 0;
    }
    return 0;
}

// Whether an actor may occupy a cell of this terrain. This is its own total
// mapping rather than a property derived from movement cost: walkability is a
// first-class terrain rule, and nothing else in the game may refuse a step onto
// walkable ground.
//
// Only two terrains block. Cliffs are the hard barrier and frame every generated
// map; deep water is the natural one. Both are visible on the map before the
// player walks into them.
[[nodiscard]] constexpr bool is_walkable(Terrain terrain) noexcept {
    switch (terrain) {
        case Terrain::open:          return true;
        case Terrain::mountain:      return true;
        case Terrain::shallow_water: return true;
        case Terrain::fields:        return true;
        case Terrain::hill:          return true;
        case Terrain::forest:        return true;
        case Terrain::deep_water:    return false;
        case Terrain::cliff:         return false;
    }
    return false;
}

// Whether this terrain blocks movement. The exact complement of is_walkable,
// named for the places that reason about barriers rather than about routes.
[[nodiscard]] constexpr bool is_barrier(Terrain terrain) noexcept {
    return !is_walkable(terrain);
}

// The canonical ASCII glyph used to serialize a terrain value.
[[nodiscard]] constexpr char symbol_of(Terrain terrain) noexcept {
    switch (terrain) {
        case Terrain::open:          return '.';
        case Terrain::mountain:      return '@';
        case Terrain::shallow_water: return '~';
        case Terrain::fields:        return 'x';
        case Terrain::hill:          return '^';
        case Terrain::forest:        return '&';
        case Terrain::deep_water:    return '=';
        case Terrain::cliff:         return '#';
    }
    return '?';
}

// The inverse of symbol_of. Returns std::nullopt for unknown symbols so callers
// can report an explicit diagnostic instead of silently inserting a default.
[[nodiscard]] constexpr std::optional<Terrain> terrain_from_symbol(char symbol) noexcept {
    switch (symbol) {
        case '.': return Terrain::open;
        case '@': return Terrain::mountain;
        case '~': return Terrain::shallow_water;
        case 'x': return Terrain::fields;
        case '^': return Terrain::hill;
        case '&': return Terrain::forest;
        case '=': return Terrain::deep_water;
        case '#': return Terrain::cliff;
        default:  return std::nullopt;
    }
}
