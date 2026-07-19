#pragma once

#include <optional>

// Terrain is the semantic type stored for every map cell. Rendering glyphs and
// passability are derived from the enum via the total functions below, so the
// core never stores presentation characters or mutates a lookup table.
//
// Walls come in two variants purely so that a map round-trips through
// serialization without losing which border glyph was used; both are equally
// impassable. A future tile-based frontend can also map them to distinct
// sprites.
enum class Terrain : unsigned char {
    open,             // '.'
    mountain,         // '@'
    water,            // '~'
    fields,           // 'x'
    hill,             // '^'
    wall_horizontal,  // '='
    wall_vertical,    // '|'
};

// Whether an actor may occupy a cell of this terrain.
[[nodiscard]] constexpr bool is_walkable(Terrain terrain) noexcept {
    switch (terrain) {
        case Terrain::open:
        case Terrain::mountain:
        case Terrain::water:
        case Terrain::fields:
        case Terrain::hill:
            return true;
        case Terrain::wall_horizontal:
        case Terrain::wall_vertical:
            return false;
    }
    return false;
}

// The canonical ASCII glyph used to serialize a terrain value.
[[nodiscard]] constexpr char symbol_of(Terrain terrain) noexcept {
    switch (terrain) {
        case Terrain::open:            return '.';
        case Terrain::mountain:        return '@';
        case Terrain::water:           return '~';
        case Terrain::fields:          return 'x';
        case Terrain::hill:            return '^';
        case Terrain::wall_horizontal: return '=';
        case Terrain::wall_vertical:   return '|';
    }
    return '?';
}

// The inverse of symbol_of. Returns std::nullopt for unknown symbols so callers
// can report an explicit diagnostic instead of silently inserting a default.
[[nodiscard]] constexpr std::optional<Terrain> terrain_from_symbol(char symbol) noexcept {
    switch (symbol) {
        case '.': return Terrain::open;
        case '@': return Terrain::mountain;
        case '~': return Terrain::water;
        case 'x': return Terrain::fields;
        case '^': return Terrain::hill;
        case '=': return Terrain::wall_horizontal;
        case '|': return Terrain::wall_vertical;
        default:  return std::nullopt;
    }
}
