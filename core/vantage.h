#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "terrain.h"

// What kind of viewpoint a vantage point turns out to be, and how far it lets the
// player see.
//
// A vantage point's identity is derived from the terrain it was placed on, in the
// same way a landmark's is (see landmark.h): the map and the name can then never
// disagree, because there is only one of them. What varies is reach. Standing on
// a mountain shows more than standing on a hill, and a marker built on level
// ground shows least of all, so the reward for walking to a vantage point depends
// on where the level had ground high enough to put one.
//
// Reach is still a square centred on the cell the actor stood on, which is the
// invariant the acceptance gate holds sight to. Only the radius changes.

enum class VantageKind {
    // A built marker on level ground, forest, or water. Modest reach, and the
    // only kind a flat level can offer.
    cairn,
    // A hilltop. The reach the prototype shipped with.
    lookout,
    // A mountain top: the widest view in the game.
    summit,
};

inline constexpr std::size_t vantage_kind_count = 3u;

inline constexpr std::array<VantageKind, vantage_kind_count> all_vantage_kinds{
    VantageKind::cairn, VantageKind::lookout, VantageKind::summit};

// A stable, non-localized identifier. Frontends choose their own wording.
[[nodiscard]] constexpr std::string_view to_string(VantageKind kind) noexcept {
    switch (kind) {
        case VantageKind::cairn:   return "cairn";
        case VantageKind::lookout: return "lookout";
        case VantageKind::summit:  return "summit";
    }
    return "cairn";
}

// The kind a vantage point takes on the given terrain. Total over every terrain
// so an unwalkable cell still answers; placement never puts content on one.
[[nodiscard]] constexpr VantageKind vantage_kind_of(Terrain terrain) noexcept {
    switch (terrain) {
        case Terrain::mountain: return VantageKind::summit;
        case Terrain::hill:     return VantageKind::lookout;
        case Terrain::open:
        case Terrain::fields:
        case Terrain::forest:
        case Terrain::shallow_water:
        case Terrain::deep_water:
        case Terrain::cliff:    return VantageKind::cairn;
    }
    return VantageKind::cairn;
}

// The one-off reveal radius the kind grants on first entry. Every value exceeds
// `visibility_radius_of` for the terrain it sits on, so climbing to a vantage
// point always shows ground that merely standing there could not.
[[nodiscard]] constexpr int vantage_reveal_radius_of(VantageKind kind) noexcept {
    switch (kind) {
        case VantageKind::cairn:   return 5;
        case VantageKind::lookout: return 9;
        case VantageKind::summit:  return 13;
    }
    return 5;
}

// How good a viewpoint the kind is, ordered so a larger rank always sees further.
// Placement uses this to prefer high ground when several cells cost the player
// the same detour.
[[nodiscard]] constexpr std::size_t vantage_rank_of(VantageKind kind) noexcept {
    switch (kind) {
        case VantageKind::cairn:   return 0u;
        case VantageKind::lookout: return 1u;
        case VantageKind::summit:  return 2u;
    }
    return 0u;
}
