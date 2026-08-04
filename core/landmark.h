#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "terrain.h"

// What a level's named landmark actually *is*. A landmark is the one authored
// thing every level guarantees, so its identity carries most of the character a
// seed has: two runs over the same tier read differently when one ends at a
// mountain overlook and the other at a river crossing.
//
// Identity is tied to terrain on purpose. Every kind belongs to exactly one
// walkable terrain, so the map always explains the name: a player who reads
// "Silent Ford Crossing" knows to look for shallow water, and a player who finds
// the landmark on a peak already knows it will be an overlook. That keeps variety
// legible instead of decorative, and it means placement and identity are the same
// decision rather than two that can disagree.
//
// This header is the single place that maps a kind to its terrain, its trailing
// noun, and its word pool, in the same total-mapping style as terrain.h. Adding a
// kind means adding a walkable terrain and updating every mapping here together.
enum class LandmarkKind {
    overlook,
    ridge_cairn,
    ford,
    grove,
    waystone,
    meadow_shrine,
};

inline constexpr std::size_t landmark_kind_count = 6u;

// Every kind in enum order. Selection iterates this so a seed's choice never
// depends on map traversal order or container iteration order.
inline constexpr std::array<LandmarkKind, landmark_kind_count> all_landmark_kinds{
    LandmarkKind::overlook, LandmarkKind::ridge_cairn,  LandmarkKind::ford,
    LandmarkKind::grove,    LandmarkKind::waystone,     LandmarkKind::meadow_shrine};

// A stable, non-localized identifier for serialization and tests. Never shown to
// a player: display wording is the frontend's business, and the generated name
// already carries the kind's noun.
[[nodiscard]] constexpr std::string_view to_string(LandmarkKind kind) noexcept {
    switch (kind) {
        case LandmarkKind::overlook:      return "overlook";
        case LandmarkKind::ridge_cairn:   return "ridge_cairn";
        case LandmarkKind::ford:          return "ford";
        case LandmarkKind::grove:         return "grove";
        case LandmarkKind::waystone:      return "waystone";
        case LandmarkKind::meadow_shrine: return "meadow_shrine";
    }
    return "waystone";
}

// The terrain a kind belongs to. Total over the enum.
[[nodiscard]] constexpr Terrain terrain_of(LandmarkKind kind) noexcept {
    switch (kind) {
        case LandmarkKind::overlook:      return Terrain::mountain;
        case LandmarkKind::ridge_cairn:   return Terrain::hill;
        case LandmarkKind::ford:          return Terrain::shallow_water;
        case LandmarkKind::grove:         return Terrain::forest;
        case LandmarkKind::waystone:      return Terrain::open;
        case LandmarkKind::meadow_shrine: return Terrain::fields;
    }
    return Terrain::open;
}

// The kind that belongs on a terrain. Total over Terrain: the impassable terrains
// answer waystone, which is unreachable in practice because a landmark always
// sits on walkable ground, but keeps the mapping total for every compiler.
[[nodiscard]] constexpr LandmarkKind landmark_kind_of(Terrain terrain) noexcept {
    switch (terrain) {
        case Terrain::open:          return LandmarkKind::waystone;
        case Terrain::fields:        return LandmarkKind::meadow_shrine;
        case Terrain::forest:        return LandmarkKind::grove;
        case Terrain::hill:          return LandmarkKind::ridge_cairn;
        case Terrain::mountain:      return LandmarkKind::overlook;
        case Terrain::shallow_water: return LandmarkKind::ford;
        case Terrain::deep_water:    return LandmarkKind::waystone;
        case Terrain::cliff:         return LandmarkKind::waystone;
    }
    return LandmarkKind::waystone;
}

// The trailing noun of a generated name: "Silent Ford *Crossing*".
[[nodiscard]] constexpr std::string_view landmark_noun(LandmarkKind kind) noexcept {
    switch (kind) {
        case LandmarkKind::overlook:      return "Overlook";
        case LandmarkKind::ridge_cairn:   return "Cairn";
        case LandmarkKind::ford:          return "Crossing";
        case LandmarkKind::grove:         return "Grove";
        case LandmarkKind::waystone:      return "Waystone";
        case LandmarkKind::meadow_shrine: return "Shrine";
    }
    return "Waystone";
}

inline constexpr std::size_t landmark_word_count = 8u;

// The middle word of a generated name, drawn from the kind's own pool so the name
// reads as one place rather than three unrelated words. Eight entries per kind
// against sixteen leading words gives 128 names per kind and 768 in total.
[[nodiscard]] constexpr std::array<std::string_view, landmark_word_count> landmark_words(
    LandmarkKind kind) noexcept {
    switch (kind) {
        case LandmarkKind::overlook:
            return {"Peak", "Crest", "Summit", "Beacon", "Spire", "Watch", "Crown", "Skyline"};
        case LandmarkKind::ridge_cairn:
            return {"Ridge", "Slope", "Shoulder", "Bluff", "Terrace", "Rise", "Knoll", "Fell"};
        case LandmarkKind::ford:
            return {"Ford", "Shallows", "Reach", "Wash", "Bank", "Current", "Narrows", "Strand"};
        case LandmarkKind::grove:
            return {"Thicket", "Canopy", "Hollow", "Glade", "Bower", "Root", "Bough", "Leaf"};
        case LandmarkKind::waystone:
            return {"Marker", "Milestone", "Cross", "Post", "Track", "Gate", "Threshold", "Compass"};
        case LandmarkKind::meadow_shrine:
            return {"Meadow", "Pasture", "Bloom", "Furrow", "Clover", "Heath", "Sward", "Blossom"};
    }
    return {"Marker", "Milestone", "Cross", "Post", "Track", "Gate", "Threshold", "Compass"};
}
