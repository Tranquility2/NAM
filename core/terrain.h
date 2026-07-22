#pragma once

#include <cstdint>
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

// The exploration/sight radius revealed around an actor standing on this
// terrain. Elevated terrain sees farther: open ground, fields, and water keep
// the base radius 2, hills reach radius 3, and mountains reach radius 4. Both
// wall variants are unoccupiable, so their radius 0 documents that an actor can
// never stand on a wall and reveal from it. This is the single source of truth
// for terrain-based visibility; GameState and tests consume it instead of
// duplicating the mapping.
[[nodiscard]] constexpr int visibility_radius_of(Terrain terrain) noexcept {
    switch (terrain) {
        case Terrain::open:            return 2;
        case Terrain::fields:          return 2;
        case Terrain::water:           return 2;
        case Terrain::hill:            return 3;
        case Terrain::mountain:        return 4;
        case Terrain::wall_horizontal: return 0;
        case Terrain::wall_vertical:   return 0;
    }
    return 0;
}

// The stamina an actor must spend to enter a cell of this terrain. This is the
// single source of truth for both movement cost and walkability: a value means
// the terrain is walkable at that cost, and std::nullopt means it cannot be
// entered at all. Both wall variants are impassable and therefore carry no cost.
[[nodiscard]] constexpr std::optional<std::uint32_t> stamina_cost_of(Terrain terrain) noexcept {
    switch (terrain) {
        case Terrain::open:            return 1;
        case Terrain::fields:          return 2;
        case Terrain::hill:            return 2;
        case Terrain::water:           return 3;
        case Terrain::mountain:        return 4;
        case Terrain::wall_horizontal: return std::nullopt;
        case Terrain::wall_vertical:   return std::nullopt;
    }
    return std::nullopt;
}

// Whether an actor may occupy a cell of this terrain. Defined in terms of
// stamina_cost_of so walkability and movement cost can never drift apart.
[[nodiscard]] constexpr bool is_walkable(Terrain terrain) noexcept {
    return stamina_cost_of(terrain).has_value();
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
