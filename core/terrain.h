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

// The daylight hours an actor spends to enter a cell of this terrain. This is the
// single source of truth for movement time, mirroring how stamina_cost_of owns
// movement stamina: entering open ground or fields costs 1 hour, entering a hill
// or water costs 2 hours, and entering a mountain costs 3 hours. Both wall
// variants are impassable and therefore carry no travel time (std::nullopt), so a
// walkable terrain always yields a value and an unwalkable one never does. This is
// consistent with stamina_cost_of: a value means walkable at that time cost.
[[nodiscard]] constexpr std::optional<std::uint32_t> travel_hours_of(Terrain terrain) noexcept {
    switch (terrain) {
        case Terrain::open:            return 1;
        case Terrain::fields:          return 1;
        case Terrain::hill:            return 2;
        case Terrain::water:           return 2;
        case Terrain::mountain:        return 3;
        case Terrain::wall_horizontal: return std::nullopt;
        case Terrain::wall_vertical:   return std::nullopt;
    }
    return std::nullopt;
}

// The stamina a single rest recovers while standing on this terrain. This is the
// single source of truth for rest recovery, mirroring how stamina_cost_of owns
// movement cost: sheltered, forageable terrain recovers more (fields 6, open 4),
// exposed terrain recovers less (hill 3, mountain 2, water 1). Both wall variants
// are unoccupiable, so they carry std::nullopt to document that an actor can never
// rest on a wall. Recovery is always capped at GameState::maximum_stamina by the
// caller; this function only owns the per-terrain amount.
[[nodiscard]] constexpr std::optional<std::uint32_t> rest_recovery_of(Terrain terrain) noexcept {
    switch (terrain) {
        case Terrain::fields:          return 6;
        case Terrain::open:            return 4;
        case Terrain::hill:            return 3;
        case Terrain::mountain:        return 2;
        case Terrain::water:           return 1;
        case Terrain::wall_horizontal: return std::nullopt;
        case Terrain::wall_vertical:   return std::nullopt;
    }
    return std::nullopt;
}

// The stamina an actor automatically regains for entering a cell of this
// terrain. Movement never fails for want of stamina: a successful step charges
// stamina_cost_of with a saturating subtraction and then adds this passive
// recovery back, capped at the maximum. The two mappings together make stamina a
// terrain-pressure meter rather than a gate — easy ground restores an expedition
// (open nets +1, fields nets 0) while rough ground drains it (hill -1, water -3,
// mountain -4). Both wall variants are unoccupiable, so they carry std::nullopt
// exactly like stamina_cost_of and rest_recovery_of.
[[nodiscard]] constexpr std::optional<std::uint32_t> passive_recovery_of(Terrain terrain) noexcept {
    switch (terrain) {
        case Terrain::open:            return 2;
        case Terrain::fields:          return 2;
        case Terrain::hill:            return 1;
        case Terrain::water:           return 0;
        case Terrain::mountain:        return 0;
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
